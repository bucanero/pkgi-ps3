#include <ppu-types.h>

int ecdsa_set_curve(u32 type);
void ecdsa_set_pub(u8 *Q);
void ecdsa_set_priv(u8 *k);
void ecdsa_sign(u8 *hash, u8 *R, u8 *S);
