/* Minimal stand-in for the autoconf-generated config.h that upstream city.cc includes.
 * Written for this project (not upstream); it provides only the two macros city.cc reads.
 */
#ifndef SAS_CITYHASH_CONFIG_H_
#define SAS_CITYHASH_CONFIG_H_

#define HAVE_BUILTIN_EXPECT 1

#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
#define WORDS_BIGENDIAN 1
#endif

#endif /* SAS_CITYHASH_CONFIG_H_ */
