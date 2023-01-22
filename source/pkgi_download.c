#include "pkgi_download.h"
#include "pkgi_dialog.h"
#include "pkgi.h"
#include "pkgi_utils.h"
#include "pkgi_sha256.h"
#include "pdb_data.h"

#include <sys/stat.h>
#include <sys/file.h>
#include <stdio.h>
#include <dirent.h>
#include <mini18n.h>


#define PDB_HDR_FILENAME	"\x00\x00\x00\xCB"
#define PDB_HDR_DATETIME	"\x00\x00\x00\xCC"
#define PDB_HDR_URL			"\x00\x00\x00\xCA"
#define PDB_HDR_ICON		"\x00\x00\x00\x6A"
#define PDB_HDR_TITLE		"\x00\x00\x00\x69"
#define PDB_HDR_SIZE		"\x00\x00\x00\xCE"
#define PDB_HDR_CONTENT		"\x00\x00\x00\xD9"
#define PDB_HDR_UNUSED		"\x00\x00\x00\x00"
#define PDB_HDR_DLSIZE		"\x00\x00\x00\xD0"


static char root[256];
static char resume_file[256];

static pkgi_http* http;
static const DbItem* db_item;
static int download_resume;

static uint64_t initial_offset;  // where http download resumes
static uint64_t download_offset; // pkg absolute offset
static uint64_t download_size;   // pkg total size (from http request)

static sha256_context sha;

static void* item_file;     // current file handle
static char item_name[256]; // current file name
static char item_path[256]; // current file path


// pkg header
static uint64_t total_size;

// UI stuff
static char dialog_extra[256];
static char dialog_eta[256];
static uint32_t info_start;
static uint32_t info_update;

static uint32_t	queue_task_id 	= 10000002;
static uint32_t	install_task_id = 80000002;


static uint32_t get_task_dir_id(const char* dir, uint32_t tid)
{
	char path[128] = "";
	int found = 0;
    struct stat sb;
	
	while (!found) {
	    pkgi_snprintf(path, sizeof(path), "%s/%d", dir, tid);

        if ((stat(path, &sb) == 0) && S_ISDIR(sb.st_mode)) {
	    	// there is already a directory with the ID, try again...
		    tid++;
		} else {
		    found = 1;
		}
    }

	return tid;
}

static void write_pdb_string(void* fp, const char* header, const char* pdbstr)
{
	unsigned int pdbstr_len = pkgi_strlen(pdbstr) + 1;

	pkgi_write(fp, header, 4);
	pkgi_write(fp, &pdbstr_len, 4);
	pkgi_write(fp, &pdbstr_len, 4);
	pkgi_write(fp, pdbstr, pdbstr_len);
}

static void write_pdb_int64(void* fp, const char* header, const uint64_t* pdb_u64)
{
	pkgi_write(fp, header, 4);
	pkgi_write(fp, "\x00\x00\x00\x08\x00\x00\x00\x08", 8);
	pkgi_write(fp, pdb_u64, 8);
}

static int create_queue_pdb_files(void)
{
	// Create files	
	char srcFile[256];
	char szIconFile[256];
	
	pkgi_snprintf(srcFile, sizeof(srcFile), "%s/%.9s.PNG", pkgi_get_temp_folder(), root + 7);
	pkgi_snprintf(szIconFile, sizeof(szIconFile), PKGI_QUEUE_FOLDER "/%d/ICON_FILE", queue_task_id);
	
	// write - ICON_FILE
	if (rename(srcFile, szIconFile) != 0)
	{
		LOG("Error saving %s", szIconFile);
		return 0;
	}
	
	pkgi_snprintf(srcFile, sizeof(srcFile), PKGI_QUEUE_FOLDER "/%d/d0.pdb", queue_task_id);
	void *fpPDB = pkgi_create(srcFile);
	if(!fpPDB)
	{
		LOG("Failed to create file %s", srcFile);
		return 0;
	}

	// write - d0.pdb
	//
	pkgi_write(fpPDB, pkg_d0top_data, d0top_data_size);
	
	// 000000CE - Download expected size (in bytes)
	write_pdb_int64(fpPDB, PDB_HDR_SIZE, &total_size);

	// 000000CB - PKG file name
	write_pdb_string(fpPDB, PDB_HDR_FILENAME, root);

	// 000000CC - date/time
	write_pdb_string(fpPDB, PDB_HDR_DATETIME, "Mon, 11 Dec 2017 11:45:10 GMT");

	// 000000CA - PKG Link download URL
	write_pdb_string(fpPDB, PDB_HDR_URL, db_item->url);

	// 0000006A - Icon location / path (PNG w/o extension) 
	write_pdb_string(fpPDB, PDB_HDR_ICON, szIconFile);

	// 00000069 - Display title	
	char title_str[256];
	pkgi_snprintf(title_str, sizeof(title_str), "\xE2\x98\x85 %s \x22%s\x22", _("Download"), db_item->name);
	write_pdb_string(fpPDB, PDB_HDR_TITLE, title_str);
	
	// 000000D9 - Content id 
	write_pdb_string(fpPDB, PDB_HDR_CONTENT, db_item->content);
	
	pkgi_write(fpPDB, pkg_d0end_data, pkg_d0end_data_size);
	pkgi_close(fpPDB);
	
	return 1;
}

static int create_install_pdb_files(const char *path, uint64_t size)
{
    void *fp1;
    void *fp2;
    char temp_buffer[256];

    pkgi_snprintf(temp_buffer, sizeof(temp_buffer), "%s/%s", path, "d0.pdb");
    fp1 = pkgi_create(temp_buffer);

    pkgi_snprintf(temp_buffer, sizeof(temp_buffer), "%s/%s", path, "d1.pdb");
    fp2 = pkgi_create(temp_buffer);

    if(!fp1 || !fp2) {
	    LOG("Failed to create file %s", temp_buffer);
	    return 0;
    }
    
	pkgi_write(fp1, install_data_pdb, install_data_pdb_size);
	pkgi_write(fp2, install_data_pdb, install_data_pdb_size);

	// 000000D0 - Downloaded size (in bytes)
	write_pdb_int64(fp1, PDB_HDR_DLSIZE, &size);
	write_pdb_int64(fp2, PDB_HDR_DLSIZE, &size);

	// 000000CE - Package expected size (in bytes)
	write_pdb_int64(fp1, PDB_HDR_SIZE, &size);
	write_pdb_int64(fp2, PDB_HDR_SIZE, &size);

	// 00000069 - Display title	
    pkgi_snprintf(temp_buffer, sizeof(temp_buffer), "\xE2\x98\x85 %s \x22%s\x22", _("Install"), db_item->name);
	write_pdb_string(fp1, PDB_HDR_TITLE, temp_buffer);
	write_pdb_string(fp2, PDB_HDR_TITLE, temp_buffer);

	// 000000CB - PKG file name
	write_pdb_string(fp1, PDB_HDR_FILENAME, root);
	write_pdb_string(fp2, PDB_HDR_FILENAME, root);

	// 00000000 - Icon location / path (PNG w/o extension) 
    pkgi_snprintf(temp_buffer, sizeof(temp_buffer), "%s/%s", path, "ICON_FILE");
	write_pdb_string(fp2, PDB_HDR_UNUSED, temp_buffer);

	// 0000006A - Icon location / path (PNG w/o extension) 
	write_pdb_string(fp1, PDB_HDR_ICON, temp_buffer);
	write_pdb_string(fp2, PDB_HDR_ICON, temp_buffer);

	pkgi_write(fp1, pkg_d0end_data, pkg_d0end_data_size);
	pkgi_write(fp2, pkg_d0end_data, pkg_d0end_data_size);

	pkgi_close(fp1);
	pkgi_close(fp2);

	pkgi_snprintf(temp_buffer, sizeof(temp_buffer), "%s/%s", path, "f0.pdb");
	pkgi_save(temp_buffer, temp_buffer, 0);

    return 1;
}

static void calculate_eta(uint32_t speed)
{
    uint64_t seconds = (download_size - download_offset) / speed;
    if (seconds < 60)
    {
        pkgi_snprintf(dialog_eta, sizeof(dialog_eta), "%s: %us", _("ETA"), (uint32_t)seconds);
    }
    else if (seconds < 3600)
    {
        pkgi_snprintf(dialog_eta, sizeof(dialog_eta), "%s: %um %02us", _("ETA"), (uint32_t)(seconds / 60), (uint32_t)(seconds % 60));
    }
    else
    {
        uint32_t hours = (uint32_t)(seconds / 3600);
        uint32_t minutes = (uint32_t)((seconds - hours * 3600) / 60);
        pkgi_snprintf(dialog_eta, sizeof(dialog_eta), "%s: %uh %02um", _("ETA"), hours, minutes);
    }
}

/* follow the CURLOPT_XFERINFOFUNCTION callback definition */
static int update_progress(void *p, int64_t dltotal, int64_t dlnow, int64_t ultotal, int64_t ulnow)
{
    uint32_t info_now = pkgi_time_msec();

    if (info_now >= info_update)
    {
        char text[256];
        pkgi_snprintf(text, sizeof(text), "%s", item_name);

        if (download_resume)
        {
            // if resuming download, then there is no "download speed"
            dialog_extra[0] = 0;
        }
        else
        {
            // report download speed
            uint32_t speed = (uint32_t)(((download_offset - initial_offset) * 1000) / (info_now - info_start));
            if (speed > 10 * 1000 * 1024)
            {
                pkgi_snprintf(dialog_extra, sizeof(dialog_extra), "%u %s/s", speed / 1024 / 1024, _("MB"));
            }
            else if (speed > 1000)
            {
                pkgi_snprintf(dialog_extra, sizeof(dialog_extra), "%u %s/s", speed / 1024, _("KB"));
            }

            if (speed != 0)
            {
                // report ETA
                calculate_eta(speed);
            }
        }

        float percent;
        if (download_resume)
        {
            // if resuming, then we may not know download size yet, use total_size from pkg header
            percent = total_size ? (float)((double)download_offset / total_size) : 0.f;
        }
        else
        {
            // when downloading use content length from http response as download size
            percent = download_size ? (float)((double)download_offset / download_size) : 0.f;
        }

        pkgi_dialog_update_progress(text, dialog_extra, dialog_eta, percent);
        info_update = info_now + 500;
    }

    return (pkgi_dialog_is_cancelled());
}

static size_t write_verify_data(void *buffer, size_t size, size_t nmemb, void *stream)
{
    size_t realsize = size * nmemb;

    if (pkgi_write(item_file, buffer, realsize))
    {
        download_offset += realsize;
        sha256_update(&sha, buffer, realsize);
        return (realsize);
    }

    return 0;
}

static int create_dummy_pkg(void)
{
	char dst[256];
	pkgi_snprintf(dst, sizeof(dst), PKGI_QUEUE_FOLDER "/%d/%s", queue_task_id, root);

	LOG("Creating empty file '%s'...", dst);
	if (!pkgi_save(dst, "PKGi PS3", 8))
		return 0;

	download_offset=0;
	if (truncate(dst, download_size) != 0)
	{
		LOG("Error truncating (%s)", dst);
		return 0;
	}

	LOG("(%s) %d bytes written", dst, download_size);
	download_offset=download_size;
    return 1;
}

static int queue_pkg_task(void)
{
    char pszPKGDir[256];
    int64_t http_length;

    LOG("requesting %s @ %llu", db_item->url, initial_offset);
    http = pkgi_http_get(db_item->url, db_item->content, initial_offset);
    if (!http)
    {
        pkgi_dialog_error(_("Could not send HTTP request"));
        return 0;
    }

    if (!pkgi_http_response_length(http, &http_length))
    {
        pkgi_dialog_error(_("HTTP request failed"));
        return 0;
    }
    if (http_length < 0)
    {
        pkgi_dialog_error(_("HTTP response has unknown length"));
        return 0;
    }

    download_size = http_length;
    total_size = download_size;

    if (!pkgi_check_free_space(http_length))
    {
        pkgi_dialog_error(_("Not enough free space on HDD"));
        return 0;
    }

	queue_task_id = get_task_dir_id(PKGI_QUEUE_FOLDER, queue_task_id);
	pkgi_snprintf(pszPKGDir, sizeof(pszPKGDir), PKGI_QUEUE_FOLDER "/%d", queue_task_id);

	if(!pkgi_mkdirs(pszPKGDir))
	{
		pkgi_dialog_error(_("Could not create task directory on HDD."));
		return 0;
	}

    LOG("http response length = %lld, total pkg size = %llu", http_length, download_size);
    info_start = pkgi_time_msec();
    info_update = pkgi_time_msec() + 500;

    pkgi_dialog_set_progress_title(_("Saving background task..."));
    pkgi_strncpy(item_name, sizeof(item_name), root);
    download_resume = 0;
    
    if(!create_dummy_pkg())
	{
		pkgi_dialog_error(_("Could not create PKG file to HDD."));
		return 0;
	}
    
	if(!create_queue_pdb_files())
	{
		pkgi_dialog_error(_("Could not create task files to HDD."));
		return 0;
	}

	return 1;
}

static void download_start(void)
{
    LOG("resuming pkg download from %llu offset", initial_offset);
    download_offset = initial_offset;
    download_resume = 0;
    info_update = pkgi_time_msec() + 1000;
    pkgi_dialog_set_progress_title(_("Downloading..."));
}

static int download_data(void)
{
    if (!http)
    {
        LOG("requesting %s @ %llu", db_item->url, initial_offset);
        http = pkgi_http_get(db_item->url, db_item->content, initial_offset);
        if (!http)
        {
            pkgi_dialog_error(_("Could not send HTTP request"));
            return 0;
        }

        int64_t http_length;
        if (!pkgi_http_response_length(http, &http_length))
        {
            pkgi_dialog_error(_("HTTP request failed"));
            return 0;
        }
        if (http_length < 0)
        {
            pkgi_dialog_error(_("HTTP response has unknown length"));
            return 0;
        }

        download_size = http_length;
        total_size = initial_offset + download_size;

        if (!pkgi_check_free_space(http_length))
        {
            LOG("error! out of space");
            return 0;
        }

        LOG("http response length = %lld, total pkg size = %llu", http_length, total_size);
        info_start = pkgi_time_msec();
        info_update = pkgi_time_msec() + 500;
    }

    if (!pkgi_http_read(http, &write_verify_data, &update_progress))
    {
        pkgi_save(resume_file, &sha, sizeof(sha));

        if (!pkgi_dialog_is_cancelled())
        {
            pkgi_dialog_error(_("HTTP download error"));
        }
        return 0;
    }

    return 1;
}

// this includes creating of all the parent folders necessary to actually create file
static int create_file(void)
{
    char folder[256];
    pkgi_strncpy(folder, sizeof(folder), item_path);
    char* last = pkgi_strrchr(folder, '/');
    *last = 0;

    if (!pkgi_mkdirs(folder))
    {
        char error[256];
        pkgi_snprintf(error, sizeof(error), "%s %s", _("cannot create folder"), folder);
        pkgi_dialog_error(error);
        return 0;
    }

    LOG("creating %s file", item_name);
    item_file = pkgi_create(item_path);
    if (!item_file)
    {
        char error[256];
        pkgi_snprintf(error, sizeof(error), "%s %s", _("cannot create file"), item_name);
        pkgi_dialog_error(error);
        return 0;
    }

    return 1;
}

static int resume_partial_file(void)
{
    LOG("resuming %s file", item_name);
    item_file = pkgi_append(item_path);
    if (!item_file)
    {
        char error[256];
        pkgi_snprintf(error, sizeof(error), "%s %s", _("cannot resume file"), item_name);
        pkgi_dialog_error(error);
        return 0;
    }

    return 1;
}

static int download_pkg_file(void)
{
    int result = 0;

    pkgi_strncpy(item_name, sizeof(item_name), root);
    pkgi_snprintf(item_path, sizeof(item_path), "%s/%s", pkgi_get_temp_folder(), root);
    LOG("downloading %s", item_name);

    if (download_resume)
    {
        initial_offset = pkgi_get_size(item_path);
        if (!resume_partial_file()) goto bail;
        download_start();
    }
    else
    {
        if (!create_file()) goto bail;
    }

    if (!download_data()) goto bail;

    LOG("%s downloaded", item_path);
    result = 1;

bail:
    if (item_file != NULL)
    {
        pkgi_close(item_file);
        item_file = NULL;
    }
    return result;
}

static int check_integrity(const uint8_t* digest)
{
    if (!digest)
    {
        LOG("no integrity provided, skipping check");
        return 1;
    }

    uint8_t check[SHA256_DIGEST_SIZE];
    sha256_finish(&sha, check);

    LOG("checking integrity of pkg");
    if (!pkgi_memequ(digest, check, SHA256_DIGEST_SIZE))
    {
        LOG("pkg integrity is wrong, removing %s & resume data", item_path);

        pkgi_rm(item_path);
        pkgi_rm(resume_file);

        pkgi_dialog_error(_("pkg integrity failed, try downloading again"));
        return 0;
    }

    LOG("pkg integrity check succeeded");
    return 1;
}

static int create_rap(const char* contentid, const uint8_t* rap)
{
    LOG("creating %s.rap", contentid);
    pkgi_dialog_update_progress(_("Creating RAP file"), NULL, NULL, 1.f);

    char path[256];
    pkgi_snprintf(path, sizeof(path), "%s/%s.rap", PKGI_RAP_FOLDER, contentid);

    if (!pkgi_save(path, rap, PKGI_RAP_SIZE))
    {
        char error[256];
        pkgi_snprintf(error, sizeof(error), "%s %s.rap", _("Cannot save"), contentid);
        pkgi_dialog_error(error);
        return 0;
    }

    LOG("RAP file created");
    return 1;
}

static int create_rif(const char* contentid, const uint8_t* rap)
{
    DIR *d;
    struct dirent *dir;
    char path[256];
    char *lic_path = NULL;

    d = opendir("/dev_hdd0/home/");
    while ((dir = readdir(d)) != NULL)
    {
        if (pkgi_strstr(dir->d_name, ".") == NULL && pkgi_strstr(dir->d_name, "..") == NULL)
        {
            pkgi_snprintf(path, sizeof(path)-1, "%s%s%s", "/dev_hdd0/home/", dir->d_name, "/exdata/act.dat");
            if (pkgi_get_size(path) > 0)
            {
                pkgi_snprintf(path, sizeof(path)-1, "%s%s%s", "/dev_hdd0/home/", dir->d_name, "/exdata/");
            	lic_path = path;
            	LOG("using folder '%s'", lic_path);
                break;
            }
        }
    }
    closedir(d);

    if (!lic_path)
    {
    	LOG("Skipping %s.rif: no act.dat file found", contentid);
    	return 1;
    }

    LOG("creating %s.rif", contentid);
    pkgi_dialog_update_progress(_("Creating RIF file"), NULL, NULL, 1.f);

    if (!rap2rif(rap, contentid, lic_path))
    {
        char error[256];
        pkgi_snprintf(error, sizeof(error), "%s %s.rif", _("Cannot save"), contentid);
        pkgi_dialog_error(error);
        return 0;
    }

    LOG("RIF file created");
    return 1;
}

int pkgi_download(const DbItem* item, const int background_dl)
{
    int result = 0;

    pkgi_snprintf(root, sizeof(root), "%s.pkg", item->content);
    LOG("package installation file: %s", root);

    pkgi_snprintf(resume_file, sizeof(resume_file), "%s/%s.resume", pkgi_get_temp_folder(), item->content);
    if (pkgi_load(resume_file, &sha, sizeof(sha)) == sizeof(sha))
    {
        LOG("resume file exists, trying to resume");
        pkgi_dialog_set_progress_title(_("Resuming..."));
        download_resume = 1;
    }
    else
    {
        LOG("cannot load resume file, starting download from scratch");
        pkgi_dialog_set_progress_title(background_dl ? _("Adding background task...") : _("Downloading..."));
        download_resume = 0;
        sha256_init(&sha);
        sha256_starts(&sha, 0);
    }

    http = NULL;
    item_file = NULL;
    download_size = 0;
    download_offset = 0;
    initial_offset = 0;
    db_item = item;

    dialog_extra[0] = 0;
    dialog_eta[0] = 0;
    info_start = pkgi_time_msec();
    info_update = info_start + 1000;

    if (item->rap)
    {
        if (!create_rap(item->content, item->rap)) goto finish;
        if (!create_rif(item->content, item->rap)) goto finish;
    }

    pkgi_dialog_update_progress(_("Downloading icon"), NULL, NULL, 1.f);
    if (!pkgi_download_icon(item->content)) goto finish;

    if (background_dl)
    {
        if (!queue_pkg_task()) goto finish;
    }
    else
    {
        if (!download_pkg_file()) goto finish;
        if (!check_integrity(item->digest)) goto finish;
    }

    pkgi_rm(resume_file);
    result = 1;

finish:
    if (http)
    {
        pkgi_http_close(http);
    }

    return result;
}

int pkgi_install(const char *titleid)
{
	char pkg_path[256];
	char filename[256];

    pkgi_snprintf(pkg_path, sizeof(pkg_path), "%s/%s", pkgi_get_temp_folder(), root);
	uint64_t fsize = pkgi_get_size(pkg_path);
    
	install_task_id = get_task_dir_id(PKGI_INSTALL_FOLDER, install_task_id);
    pkgi_snprintf(pkg_path, sizeof(pkg_path), PKGI_INSTALL_FOLDER "/%d", install_task_id);

	if (!pkgi_mkdirs(pkg_path))
	{
		pkgi_dialog_error(_("Could not create install directory on HDD."));
		return 0;
	}

	LOG("Creating .pdb files [%s]", titleid);

	// write - ICON_FILE
	pkgi_snprintf(filename, sizeof(filename), "%s/ICON_FILE", pkg_path);
	pkgi_snprintf(resume_file, sizeof(resume_file), "%s/%s.PNG", pkgi_get_temp_folder(), titleid);
	if (rename(resume_file, filename) != 0)
	{
	    LOG("Error saving %s", filename);
	    return 0;
    }

    if (!create_install_pdb_files(pkg_path, fsize)) {
        return 0;
    }

    pkgi_snprintf(filename, sizeof(filename), "%s/%s", pkg_path, root);
    pkgi_snprintf(pkg_path, sizeof(pkg_path), "%s/%s", pkgi_get_temp_folder(), root);
    
    LOG("move (%s) -> (%s)", pkg_path, filename);
    
	return (rename(pkg_path, filename) == 0);
}

int pkgi_download_icon(const char* content)
{
    char icon_url[256];
    char icon_file[256];
    uint8_t hmac[20];
    uint32_t sz;

    pkgi_snprintf(icon_file, sizeof(icon_file), PKGI_TMP_FOLDER "/%.9s.PNG", content + 7);
    LOG("package icon file: %s", icon_file);

    if (pkgi_get_size(icon_file) > 0)
        return 1;

    pkgi_snprintf(icon_url, sizeof(icon_url), "%.9s_00", content + 7);
    sha1_hmac(tmdb_hmac_key, sizeof(tmdb_hmac_key), (uint8_t*) icon_url, 12, hmac);

    pkgi_snprintf(icon_url, sizeof(icon_url), "http://tmdb.np.dl.playstation.net/tmdb/%.9s_00_%llX%llX%X/ICON0.PNG", 
        content + 7,
        ((uint64_t*)hmac)[0], 
        ((uint64_t*)hmac)[1], 
        ((uint32_t*)hmac)[4]);

    char * buffer = pkgi_http_download_buffer(icon_url, &sz);

    if (!buffer)
    {
        LOG("http request to %s failed", icon_url);
        return pkgi_save(icon_file, iconfile_data, iconfile_data_size);
    }

    if (!sz)
    {
        LOG("icon not found, using default");
        free(buffer);
        return pkgi_save(icon_file, iconfile_data, iconfile_data_size);
    }

    LOG("received %u bytes", sz);

    pkgi_save(icon_file, buffer, sz);
    free(buffer);

    return 1;
}
