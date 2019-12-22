#include "pkgi_download.h"
#include "pkgi_dialog.h"
#include "pkgi.h"
#include "pkgi_utils.h"
#include "pkgi_sha256.h"
#include "pdb_data.h"

#include <sys/stat.h>
#include <sys/file.h>
#include <lv2/sysfs.h>

#define BUFF_SIZE  0x200000 // 2MB

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

static sha256_ctx sha;

static void* item_file;     // current file handle
static char item_name[256]; // current file name
static char item_path[256]; // current file path


// temporary buffer for downloads
static uint8_t down[64 * 1024];

// pkg header
static uint64_t total_size;


// UI stuff
static char dialog_extra[256];
static char dialog_eta[256];
static uint32_t info_start;
static uint32_t info_update;

static uint32_t	queue_task_id 	= 10000002;
static uint32_t	install_task_id = 80000002;


// Async IO stuff
#define AIO_BUFFERS		4
#define AIO_NUMBER		2
#define AIO_FAILED 		0
#define AIO_READY 		1
#define AIO_BUSY  		2

static sysFSAio aio_write[AIO_NUMBER];

uint8_t write_status[AIO_NUMBER];
uint8_t buffer_to_write;


uint32_t get_task_dir_id(const char* dir, uint32_t tid)
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
	pkgi_write(fp, header, 4);
    
	unsigned int pdbstr_len = pkgi_strlen(pdbstr) + 1;
	pkgi_write(fp, (char*) &pdbstr_len, 4);
	pkgi_write(fp, (char*) &pdbstr_len, 4);
	pkgi_write(fp, pdbstr, pdbstr_len);
}

static int create_queue_pdb_files(void)
{
	// Create files	
	char szPDBFile[256] = "";
	char szIconFile[256] = "";
	
	pkgi_snprintf(szPDBFile, sizeof(szPDBFile), PKGI_QUEUE_FOLDER "/%d/d0.pdb", queue_task_id);
	pkgi_snprintf(szIconFile, sizeof(szIconFile), PKGI_QUEUE_FOLDER "/%d/ICON_FILE", queue_task_id);
	
	// write - ICON_FILE
	if (!pkgi_save(szIconFile, iconfile_data, iconfile_data_size))
	{
	    LOG("Error saving %s", szIconFile);
	    return 0;
    }
	
	void *fpPDB = pkgi_create(szPDBFile);
	if(!fpPDB)
	{
	    LOG("Failed to create file %s", szPDBFile);
		return 0;
	}

	// write - d0.pdb
	//
    pkgi_write(fpPDB, pkg_d0top_data, d0top_data_size);
	
	// 000000CE - Download expected size (in bytes)
	pkgi_write(fpPDB, PDB_HDR_SIZE "\x00\x00\x00\x08\x00\x00\x00\x08", 12);
	pkgi_write(fpPDB, (char*) &total_size, 8);

	// 000000CB - PKG file name
	write_pdb_string(fpPDB, PDB_HDR_FILENAME, root);

	// 000000CC - date/time
	write_pdb_string(fpPDB, PDB_HDR_DATETIME, "Mon, 11 Dec 2017 11:45:10 GMT");

	// 000000CA - PKG Link download URL
	write_pdb_string(fpPDB, PDB_HDR_URL, db_item->url);

	// 0000006A - Icon location / path (PNG w/o extension) 
	write_pdb_string(fpPDB, PDB_HDR_ICON, szIconFile);

	// 00000069 - Display title	
	char title_str[256] = "";
	pkgi_snprintf(title_str, sizeof(title_str), "\xE2\x98\x85 Download \x22%s\x22", db_item->name);
	write_pdb_string(fpPDB, PDB_HDR_TITLE, title_str);
	
	// 000000D9 - Content id 
	write_pdb_string(fpPDB, PDB_HDR_CONTENT, db_item->content);
	
	pkgi_write(fpPDB, pkg_d0end_data, pkg_d0end_data_size);
	pkgi_close(fpPDB);
	
	return 1;
}

int create_install_pdb_files(char *path, char *title, char *path_icon, uint64_t size)
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
	pkgi_write(fp1, PDB_HDR_DLSIZE "\x00\x00\x00\x08\x00\x00\x00\x08", 12);
	pkgi_write(fp1, (char*) &size, 8);

	pkgi_write(fp2, PDB_HDR_DLSIZE "\x00\x00\x00\x08\x00\x00\x00\x08", 12);
	pkgi_write(fp2, (char*) &size, 8);

	// 000000CE - Package expected size (in bytes)
	pkgi_write(fp1, PDB_HDR_SIZE "\x00\x00\x00\x08\x00\x00\x00\x08", 12);
	pkgi_write(fp1, (char*) &size, 8);

	pkgi_write(fp2, PDB_HDR_SIZE "\x00\x00\x00\x08\x00\x00\x00\x08", 12);
	pkgi_write(fp2, (char*) &size, 8);

	// 00000069 - Display title	
    pkgi_snprintf(temp_buffer, sizeof(temp_buffer), "\xE2\x98\x85 Install \x22%s\x22", title);
	write_pdb_string(fp1, PDB_HDR_TITLE, temp_buffer);
	write_pdb_string(fp2, PDB_HDR_TITLE, temp_buffer);

	// 000000CB - PKG file name
	write_pdb_string(fp1, PDB_HDR_FILENAME, title);
	write_pdb_string(fp2, PDB_HDR_FILENAME, title);

	// 00000000 - Icon location / path (PNG w/o extension) 
	write_pdb_string(fp2, PDB_HDR_UNUSED, path_icon);

	// 0000006A - Icon location / path (PNG w/o extension) 
	write_pdb_string(fp1, PDB_HDR_ICON, path_icon);
	write_pdb_string(fp2, PDB_HDR_ICON, path_icon);

	pkgi_write(fp1, pkg_d0end_data, pkg_d0end_data_size);
	pkgi_write(fp2, pkg_d0end_data, pkg_d0end_data_size);

	pkgi_close(fp1);
	pkgi_close(fp2);

	pkgi_snprintf(temp_buffer, sizeof(temp_buffer), "%s/%s", path, "f0.pdb");
	fp1 = pkgi_create(temp_buffer);
	if (fp1) pkgi_close(fp1);

    return 1;
}

static void calculate_eta(uint32_t speed)
{
    uint64_t seconds = (download_size - download_offset) / speed;
    if (seconds < 60)
    {
        pkgi_snprintf(dialog_eta, sizeof(dialog_eta), "ETA: %us", (uint32_t)seconds);
    }
    else if (seconds < 3600)
    {
        pkgi_snprintf(dialog_eta, sizeof(dialog_eta), "ETA: %um %02us", (uint32_t)(seconds / 60), (uint32_t)(seconds % 60));
    }
    else
    {
        uint32_t hours = (uint32_t)(seconds / 3600);
        uint32_t minutes = (uint32_t)((seconds - hours * 3600) / 60);
        pkgi_snprintf(dialog_eta, sizeof(dialog_eta), "ETA: %uh %02um", hours, minutes);
    }
}

static void update_progress(void)
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
                pkgi_snprintf(dialog_extra, sizeof(dialog_extra), "%u MB/s", speed / 1024 / 1024);
            }
            else if (speed > 1000)
            {
                pkgi_snprintf(dialog_extra, sizeof(dialog_extra), "%u KB/s", speed / 1024);
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
}


static void writing_callback(sysFSAio *xaio, s32 error, s32 xid, u64 size)
{
	int i = xaio->usrdata;
	
	if(error != 0) {
		write_status[i] = AIO_FAILED;
		LOG("Error : writing error %X", (unsigned int) error);
	} else 
	if(size != xaio->size) {
		write_status[i] = AIO_FAILED;
		LOG("Error : writing size %X / %X", (unsigned int) size, (unsigned int) xaio->size);
	} else {
		buffer_to_write++;
		if (buffer_to_write == AIO_BUFFERS) buffer_to_write=0;
		write_status[i] = AIO_READY;
		download_offset+=size;
	}
}

static int create_dummy_pkg(void)
{
    int fdw, i;
	static int id_w[2] = {-1, -1};
	
	char dst[256] ="";
	pkgi_snprintf(dst, sizeof(dst), PKGI_QUEUE_FOLDER "/%d/%s", queue_task_id, root);

    if(sysFsAioInit(dst)!= 0)  {
		LOG("Error : AIO_FAILED to copy_async / sysFsAioInit(dst)");
		return AIO_FAILED;
	}

	if(sysFsOpen(dst, SYS_O_CREAT | SYS_O_TRUNC | SYS_O_WRONLY, &fdw, 0, 0) != 0) {
		LOG("Error : AIO_FAILED to copy_async / sysFsOpen(src)");
		return AIO_FAILED;
	}

	char *mem = (char *) pkgi_malloc(AIO_BUFFERS * BUFF_SIZE);
	if(mem == NULL) {
		LOG("Error : AIO_FAILED to copy_async / malloc");
		return AIO_FAILED;
	}
	
	for(i=0; i < AIO_NUMBER; i++) {
		aio_write[i].fd = -1;
		write_status[i]=AIO_READY;
	}

	uint64_t writing_pos=0ULL;		
	buffer_to_write=0;
	download_offset=0;
	
    while(download_offset < download_size)
    {
    	i = !i;

		if((write_status[i] == AIO_READY) && (writing_pos < download_size))
		{
			aio_write[i].fd = fdw;
			aio_write[i].offset = writing_pos;
			aio_write[i].buffer_addr = (u32) (u64) &mem[buffer_to_write * BUFF_SIZE];
			aio_write[i].size = min64(BUFF_SIZE, download_size - writing_pos);
			aio_write[i].usrdata = i;
									
			write_status[i] = AIO_BUSY;
			writing_pos += aio_write[i].size;
						
			if(sysFsAioWrite(&aio_write[i], &id_w[i], writing_callback) != 0) {
				LOG("Error : AIO_FAILED to copy_async / sysFsAioWrite");
				goto error;
			}
		}
			
		if(write_status[i] == AIO_FAILED || pkgi_dialog_is_cancelled()) {
			LOG("Error : AIO_FAILED to copy_async / write_status = AIO_FAILED !");
			goto error;
		}
			
		update_progress();
    }
	
	for(i=0; i<AIO_NUMBER; i++) {
		sysFsClose(aio_write[i].fd);
	}
	sysFsAioFinish("/dev_hdd0");
    pkgi_free(mem);
	
    return 1;

error:
	for(i=0; i<AIO_NUMBER; i++) {
		sysFsAioCancel(id_w[i]);
	}
	for(i=0; i<AIO_NUMBER; i++) {
		sysFsClose(aio_write[i].fd);
	}

	sysFsAioFinish(dst);
    pkgi_free(mem);

    return AIO_FAILED;
}


static int queue_pkg_task()
{
	char pszPKGDir[256] ="";
	queue_task_id = get_task_dir_id(PKGI_QUEUE_FOLDER, queue_task_id);
	pkgi_snprintf(pszPKGDir, sizeof(pszPKGDir), PKGI_QUEUE_FOLDER "/%d", queue_task_id);

	if(!pkgi_mkdirs(pszPKGDir))
	{
		pkgi_dialog_error("Could not create task directory on HDD.");
		return 0;
	}
	
    initial_offset = 0;
    LOG("requesting %s @ %llu", db_item->url, 0);
    http = pkgi_http_get(db_item->url, db_item->content, 0);
    if (!http)
    {
    	pkgi_dialog_error("Could not send HTTP request");
        return 0;
    }

    int64_t http_length;
    if (!pkgi_http_response_length(http, &http_length))
    {
        pkgi_dialog_error("HTTP request failed");
        return 0;
    }
    if (http_length < 0)
    {
        pkgi_dialog_error("HTTP response has unknown length");
        return 0;
    }

    download_size = http_length;
    total_size = download_size;

    if (!pkgi_check_free_space(http_length))
    {
        pkgi_dialog_error("Not enough free space on HDD");
        return 0;
    }

    LOG("http response length = %lld, total pkg size = %llu", http_length, download_size);
    info_start = pkgi_time_msec();
    info_update = pkgi_time_msec() + 500;

    pkgi_dialog_set_progress_title("Saving PKG...");
    pkgi_strncpy(item_name, sizeof(item_name), root);
    download_resume = 0;
    
    if(!create_dummy_pkg())
	{
		pkgi_dialog_error("Could not create PKG file to HDD.");
		return 0;
	}
    
	if(!create_queue_pdb_files())
	{
		pkgi_dialog_error("Could not create task files to HDD.");
		return 0;
	}

	return 1;
}


static void download_start(void)
{
    LOG("resuming pkg download from %llu offset", download_offset);
    download_resume = 0;
    info_update = pkgi_time_msec() + 1000;
    pkgi_dialog_set_progress_title("Downloading...");
}

static int download_data(uint8_t* buffer, uint32_t size, int save)
{
    if (pkgi_dialog_is_cancelled())
    {
        pkgi_save(resume_file, &sha, sizeof(sha));
        return 0;
    }

    update_progress();

    if (!http)
    {
        initial_offset = download_offset;
        LOG("requesting %s @ %llu", db_item->url, download_offset);
        http = pkgi_http_get(db_item->url, db_item->content, download_offset);
        if (!http)
        {
            pkgi_dialog_error("Could not send HTTP request");
            return 0;
        }

        int64_t http_length;
        if (!pkgi_http_response_length(http, &http_length))
        {
            pkgi_dialog_error("HTTP request failed");
            return 0;
        }
        if (http_length < 0)
        {
            pkgi_dialog_error("HTTP response has unknown length");
            return 0;
        }

        download_size = http_length + download_offset;
        total_size = download_size;

        if (!pkgi_check_free_space(http_length))
        {
            return 0;
        }

        LOG("http response length = %lld, total pkg size = %llu", http_length, download_size);
        info_start = pkgi_time_msec();
        info_update = pkgi_time_msec() + 500;
    }
        
    int read = pkgi_http_read(http, buffer, size);
    if (read < 0)
    {
        char error[256];
        pkgi_snprintf(error, sizeof(error), "HTTP download error 0x%08x", read);
        pkgi_dialog_error(error);
        pkgi_save(resume_file, &sha, sizeof(sha));
        return -1;
    }
    else if (read == 0)
    {
        pkgi_dialog_error("HTTP connection closed");
        pkgi_save(resume_file, &sha, sizeof(sha));
        return -1;
    }
    download_offset += read;

    sha256_update(&sha, buffer, read);

    if (save)
    {
        if (!pkgi_write(item_file, buffer, read))
        {
            char error[256];
            pkgi_snprintf(error, sizeof(error), "failed to write to %s", item_path);
            pkgi_dialog_error(error);
            return -1;
        }
    }
    
    return read;
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
        pkgi_snprintf(error, sizeof(error), "cannot create folder %s", folder);
        pkgi_dialog_error(error);
        return 0;
    }

    LOG("creating %s file", item_name);
    item_file = pkgi_create(item_path);
    if (!item_file)
    {
        char error[256];
        pkgi_snprintf(error, sizeof(error), "cannot create file %s", item_name);
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
        pkgi_snprintf(error, sizeof(error), "cannot resume file %s", item_name);
        pkgi_dialog_error(error);
        return 0;
    }

    return 1;
}

static int download_pkg_file(void)
{
    LOG("downloading %s", root);

    int result = 0;

    pkgi_strncpy(item_name, sizeof(item_name), root);
    pkgi_snprintf(item_path, sizeof(item_path), "%s/%s", PKGI_PKG_FOLDER, root);

    if (download_resume)
    {
        download_offset = pkgi_get_size(item_path);
        if (!resume_partial_file()) goto bail;
        download_start();
    }
    else
    {
        if (!create_file()) goto bail;
    }

    total_size = sizeof(down);//download_size;
//    while (size > 0)
    while (download_offset != total_size)
    {
        uint32_t read = (uint32_t)min64(sizeof(down), total_size - download_offset);
        int size = download_data(down, read, 1);
        
        if (size <= 0)
        {
            goto bail;
        }
    }

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

        pkgi_dialog_error("pkg integrity failed, try downloading again");
        return 0;
    }

    LOG("pkg integrity check succeeded");
    return 1;
}

static int create_rap(const char* contentid, const uint8_t* rap)
{
    LOG("creating %s.rap", contentid);
    pkgi_dialog_update_progress("Creating RAP file", NULL, NULL, 1.f);

    char path[256];
    pkgi_snprintf(path, sizeof(path), "%s", PKGI_RAP_FOLDER);

    if (!pkgi_mkdirs(path))
    {
        char error[256];
        pkgi_snprintf(error, sizeof(error), "Cannot create folder %s", PKGI_RAP_FOLDER);
        pkgi_dialog_error(error);
        return 0;
    }

    pkgi_snprintf(path, sizeof(path), "%s/%s.rap", PKGI_RAP_FOLDER, contentid);

    if (!pkgi_save(path, rap, PKGI_RAP_SIZE))
    {
        char error[256];
        pkgi_snprintf(error, sizeof(error), "Cannot save %s.rap", contentid);
        pkgi_dialog_error(error);
        return 0;
    }

    LOG("RAP file created");
    return 1;
}

int pkgi_download(const DbItem* item, const int background_dl)
{
    int result = 0;

    pkgi_snprintf(root, sizeof(root), "%.9s.pkg", item->content + 7);
    LOG("package installation file: %s", root);

    pkgi_snprintf(resume_file, sizeof(resume_file), "%s/%.9s.resume", pkgi_get_temp_folder(), item->content + 7);
    if (pkgi_load(resume_file, &sha, sizeof(sha)) == sizeof(sha))
    {
        LOG("resume file exists, trying to resume");
        pkgi_dialog_set_progress_title("Resuming...");
        download_resume = 1;
    }
    else
    {
        LOG("cannot load resume file, starting download from scratch");
        pkgi_dialog_set_progress_title("Downloading...");
        download_resume = 0;
        sha256_init(&sha);
    }

    http = NULL;
    item_file = NULL;
    download_size = 0;
    download_offset = 0;
    db_item = item;

    dialog_extra[0] = 0;
    dialog_eta[0] = 0;
    info_start = pkgi_time_msec();
    info_update = info_start + 1000;

	if (background_dl)
	{
    	if (!queue_pkg_task()) goto finish;
	}
	else
	{
	    if (!download_pkg_file()) goto finish;
	    if (!check_integrity(item->digest)) goto finish;
	}
    if (item->rap)
    {
        if (!create_rap(item->content, item->rap)) goto finish;
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
	char self_path[256];
	char szIconFile[256];
	char filename[256];

    pkgi_snprintf(filename, sizeof(filename), "%s.pkg", titleid);

    pkgi_snprintf(self_path, sizeof(self_path), PKGI_PKG_FOLDER "/%s.pkg", titleid);
	uint64_t fsize = pkgi_get_size(self_path);
    
	install_task_id = get_task_dir_id(PKGI_INSTALL_FOLDER, install_task_id);
    pkgi_snprintf(self_path, sizeof(self_path), PKGI_INSTALL_FOLDER "/%d", install_task_id);

	if (!pkgi_mkdirs(self_path))
	{
		pkgi_dialog_error("Could not create install directory on HDD.");
		return 0;
	}

    pkgi_snprintf(szIconFile, sizeof(szIconFile), PKGI_INSTALL_FOLDER "/%d/ICON_FILE", install_task_id);

	// write - ICON_FILE
	if (!pkgi_save(szIconFile, iconfile_data, iconfile_data_size))
	{
	    LOG("Error saving %s", szIconFile);
	    return 0;
    }

    if (!create_install_pdb_files(self_path, filename, szIconFile, fsize)) {
        return 0;
    }

    pkgi_snprintf(filename, sizeof(filename), "%s/%s.pkg", self_path, titleid);
    pkgi_snprintf(self_path, sizeof(self_path), PKGI_INSTALL_FOLDER "/%s.pkg", titleid);
    
    int ret = sysLv2FsRename(self_path, filename);

	return (ret == 0);
}
