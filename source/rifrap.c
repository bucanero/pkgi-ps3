// (c) flatz
// http://web.archive.org/web/20141118220924/http://pastie.org/private/9hjpnaewxg5twytosnx4w
// http://web.archive.org/web/20141118183317/http://pastie.org/private/pmnmsnqg6zbfnk9xactbw
// http://web.archive.org/web/20141117072342/http://pastie.org/private/yltlfwubsz8w5pyhmojyfg
//
// PS3Xploit-Resign (PS3XploitTeam)
// https://github.com/PS3Xploit/PS3xploit-resigner

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ppu-lv2.h>
#include <polarssl/aes.h>
#include <polarssl/sha1.h>

#include "pkgi.h"
#include "pkgi_utils.h"

#define SYS_SS_APPLIANCE_INFO_MANAGER              867
#define AIM_GET_DEVICE_ID                          0x19003

int ecdsa_set_curve(u32 type);
void ecdsa_set_pub(u8 *Q);
void ecdsa_set_priv(u8 *k);
void ecdsa_sign_rif(u8 *hash, u8 *R, u8 *S);

struct rif
{
	uint32_t version;
	uint32_t licenseType;
	uint64_t accountid;
    char titleid[0x30]; //Content ID
    uint8_t padding[0xC]; //Padding for randomness
    uint32_t actDatIndex; //Key index on act.dat between 0x00 and 0x7F
    uint8_t key[0x10]; //encrypted klicensee
    uint64_t timestamp; //timestamp??
    uint64_t expiration; //Always 0
    uint8_t r[0x14];
    uint8_t s[0x14];
} __attribute__ ((packed));

struct actdat
{
	uint32_t version;
	uint32_t licenseType;
	uint8_t accountId[8];
    uint8_t keyTable[0x800]; //Key Table
    uint8_t unk2[0x800];
    uint8_t signature[0x28];
} __attribute__ ((packed));


const uint8_t rap_initial_key[16] = {
    0x86, 0x9F, 0x77, 0x45, 0xC1, 0x3F, 0xD8, 0x90, 
    0xCC, 0xF2, 0x91, 0x88, 0xE3, 0xCC, 0x3E, 0xDF
};
const uint8_t pbox[16] = {
    0x0C, 0x03, 0x06, 0x04, 0x01, 0x0B, 0x0F, 0x08, 
    0x02, 0x07, 0x00, 0x05, 0x0A, 0x0E, 0x0D, 0x09
};
const uint8_t e1[16] = {
    0xA9, 0x3E, 0x1F, 0xD6, 0x7C, 0x55, 0xA3, 0x29, 
    0xB7, 0x5F, 0xDD, 0xA6, 0x2A, 0x95, 0xC7, 0xA5
};
const uint8_t e2[16] = {
    0x67, 0xD4, 0x5D, 0xA3, 0x29, 0x6D, 0x00, 0x6A, 
    0x4E, 0x7C, 0x53, 0x7B, 0xF5, 0x53, 0x8C, 0x74
};

// npdrm_const /klicenseeConst
const uint8_t idps_key_const[16] = {
    0x5E, 0x06, 0xE0, 0x4F, 0xD9, 0x4A, 0x71, 0xBF, 
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01 
};

// NP_rif_key /actdatIndexDecKey
const uint8_t rif_key_const[16] = {
    0xDA, 0x7D, 0x4B, 0x5E, 0x49, 0x9A, 0x4F, 0x53, 
    0xB1, 0xC1, 0xA1, 0x4A, 0x74, 0x84, 0x44, 0x3B 
};

uint8_t ec_k_nm[21] = {0x00, 0xbf, 0x21, 0x22, 0x4b, 0x04, 0x1f, 0x29, 0x54, 0x9d, 
						0xb2, 0x5e, 0x9a, 0xad, 0xe1, 0x9e, 0x72, 0x0a, 0x1f, 0xe0, 0xf1};
uint8_t ec_Q_nm[40] = {0x94, 0x8D, 0xA1, 0x3E, 0x8C, 0xAF, 0xD5, 0xBA, 0x0E, 0x90,
						0xCE, 0x43, 0x44, 0x61, 0xBB, 0x32, 0x7F, 0xE7, 0xE0, 0x80,
						0x47, 0x5E, 0xAA, 0x0A, 0xD3, 0xAD, 0x4F, 0x5B, 0x62, 0x47,
						0xA7, 0xFD, 0xA8, 0x6D, 0xF6, 0x97, 0x90, 0x19, 0x67, 0x73};


int sys_ss_appliance_info_manager(uint32_t packet_id, uint64_t arg)
{
    lv2syscall2(SYS_SS_APPLIANCE_INFO_MANAGER, (uint64_t)packet_id, (uint64_t)arg);
    return_to_user_prog(int);
}

int ss_aim_get_device_id(uint8_t *idps)
{
    return sys_ss_appliance_info_manager(AIM_GET_DEVICE_ID, (uint64_t)idps);
}

void aesecb128_encrypt(const uint8_t *key, const uint8_t *in, uint8_t *out)
{
	aes_context ctx;
	aes_setkey_enc(&ctx, key, 128);
	aes_crypt_ecb(&ctx, AES_ENCRYPT, in, out);
}

void aesecb128_decrypt(const uint8_t *key, const uint8_t *in, uint8_t *out)
{
	aes_context ctx;
	aes_setkey_dec(&ctx, key, 128);
	aes_crypt_ecb(&ctx, AES_DECRYPT, in, out);
}

int rap_to_klicensee(const uint8_t *rap_key, uint8_t *klicensee)
{
	int round_num;
	int i;

	uint8_t key[16];
	aesecb128_decrypt(rap_initial_key, rap_key, key);

	for (round_num = 0; round_num < 5; ++round_num) {
		for (i = 0; i < 16; ++i) {
			int p = pbox[i];
			key[p] ^= e1[p];
		}
		for (i = 15; i >= 1; --i) {
			int p = pbox[i];
			int pp = pbox[i - 1];
			key[p] ^= key[pp];
		}
		int o = 0;
		for (i = 0; i < 16; ++i) {
			int p = pbox[i];
			uint8_t kc = key[p] - o;
			uint8_t ec2 = e2[p];
			if (o != 1 || kc != 0xFF) {
				o = kc < ec2 ? 1 : 0;
				key[p] = kc - ec2;
			} else if (kc == 0xFF) {
				key[p] = kc - ec2;
			} else {
				key[p] = kc;
			}
		}
	}

	memcpy(klicensee, key, sizeof(key));
	return 0;
}

struct actdat *actdat_get(const char* base)
{
	char path[256];
    struct actdat *actdat;

    actdat = malloc(sizeof(struct actdat));
	if (actdat == NULL)
		goto fail;

    snprintf(path, sizeof(path), "%s" "act.dat", base);

    LOG("Loading '%s'...", path);
    if (pkgi_load(path, (uint8_t*) actdat, sizeof(struct actdat)) < 0)
        goto fail;

    return actdat;

fail:
	if (actdat != NULL) {
		free(actdat);
	}

	return NULL; 
}

int rap2rif(const uint8_t* rap, const char* content_id, const char *exdata_path)
{
	struct actdat *actdat = NULL;
	struct rif rif;
	uint8_t idps[0x10];
	uint8_t idps_const[0x10];
	uint8_t act_dat_key[0x10];
	uint8_t sha1_digest[20];
	uint8_t R[0x15];
	uint8_t S[0x15];
	char path[256];

    actdat = actdat_get(exdata_path);
	if (actdat == NULL) {
		LOG("Error: unable to load act.dat");
		goto fail;
	}

	LOG("Reading IDPS ...");
	ss_aim_get_device_id(idps);

	memset(&rif, 0, sizeof(struct rif));
	
	rif.version = 1;
	rif.licenseType = 0x00010002;
	rif.timestamp = 0x0000012F415C0000;
	rif.expiration = 0;
	rif.accountid = get64be(actdat->accountId);
	strncpy(rif.titleid, content_id, sizeof(rif.titleid));

	//convert rap to rifkey(klicensee)
	rap_to_klicensee(rap, rif.key);
	aesecb128_encrypt(idps, idps_key_const, idps_const);
	aesecb128_decrypt(idps_const, actdat->keyTable, act_dat_key);
	
	//encrypt rif with act.dat first key primary key table
	aesecb128_encrypt(act_dat_key, rif.key, rif.key);
	aesecb128_encrypt(rif_key_const, rif.padding, rif.padding);

	sha1((uint8_t*) &rif, 0x70, sha1_digest);
	ecdsa_set_curve(0);
	ecdsa_set_pub(ec_Q_nm);
	ecdsa_set_priv(ec_k_nm);
	ecdsa_sign_rif(sha1_digest, R, S);

	memcpy(rif.r, R+1, sizeof(rif.r));
	memcpy(rif.s, S+1, sizeof(rif.s));

    snprintf(path, sizeof(path), "%s%s.rif", exdata_path, content_id);

	LOG("Saving rif to '%s'...", path);
	if (pkgi_save(path, (uint8_t*) &rif, sizeof(struct rif)) < 0) {
		LOG("Error: unable to create rif file");
		goto fail;
	}

	free(actdat);
	return 1;

fail:
	if (actdat != NULL) {
		free(actdat);
	}

	return 0;
}
