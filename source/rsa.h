#pragma once

#ifndef __RSA_H__
#define __RSA_H__

#include <switch.h>

bool rsa_sign(void* input, size_t input_size, unsigned char* output, size_t output_size);
const unsigned char *rsa_get_public_key();

#endif
