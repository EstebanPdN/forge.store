#pragma once
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <openssl/sha.h>
#define DATA_DIR "sdmc:/3ds/forge-store"
static int mbedtls_sha256_ret(const unsigned char*p,size_t n,unsigned char*out,int sha224){(void)sha224;return SHA256(p,n,out)?0:-1;}
