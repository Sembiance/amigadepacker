/*
   StoneCracker S404 algorithm data decompression routine
   (c) 2006 Jouni 'Mr.Spiv' Korhonen
  
   v0.1

   from shd:
   Some portability notes. We are using int32_t as a file size, and that fits
   all Amiga file sizes. size_t is of course the right choice.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <netinet/in.h>
#include <assert.h>


static int _bc;
static uint32_t _bb;
static uint8_t *_org_src;

int initGetb(uint8_t **b)
{
  int eff;
  _bc = ntohs(* (uint16_t *) (*b));
  *b -= 2;    /* bit counter */

  _bb = ntohs(* (uint16_t *) (*b));
  *b -= 2;    /* last short  */

  eff = ntohs(* (uint16_t *) (*b));
  *b -= 2;    /* efficiency */

  /* check for _bc validity as it may be bugged */
  _bc &= 0x000f;

  return eff;
}

uint16_t getb(uint8_t **b, int l)
{
  _bb &= 0x0000ffff;

  if (_bc < l) {
    _bb <<= _bc;

    assert((intptr_t) *b >= (intptr_t) _org_src);

    _bb |= ((*b)[0] << 8);
    _bb |= (*b)[1];
    *b -= 2;

    l -= _bc;
    _bc = 16;
  }
  _bc -= l;
  _bb <<= l;
  return (_bb >> 16);
}

/* Returns bytes still to read.. or < 0 if error. */

size_t checkS404File( FILE *fh, int32_t *oLen, int32_t *pLen, int32_t *sLen )
{
  uint8_t buf[16];
  size_t len;

  fseek(fh, 0, SEEK_END);
  len = ftell(fh);
  fseek(fh, 0, SEEK_SET);

  if (fread(buf, 1, sizeof buf, fh) != sizeof buf)
    return -1;
  if (memcmp(buf, "S404", 4))
    return -2;

  *sLen = ntohl(* (uint32_t *) &buf[4]);
  assert(*sLen >= 0);
  *oLen = ntohl(* (uint32_t *) &buf[8]); /* Depacked length */
  assert(*oLen >= 0);
  *pLen = ntohl(* (uint32_t *) &buf[12]); /* Packed length */
  assert(*pLen >= 0);

  return len - 16;
}


void decompressS404(uint8_t *src, uint8_t *orgdst,
		    int32_t dst_length, int32_t src_length)
{
  uint16_t w;
  int32_t eff;
  int32_t n;
  uint8_t *dst;
  int32_t oLen = dst_length;
  int32_t pLen = src_length;

  _org_src = src;

  src += pLen;
  dst = orgdst + oLen;
  eff = initGetb(&src);

  /*printf("_bc: %02X, _bb: %04X, eff: %d\n",_bc,_bb,eff);*/

  while (oLen > 0) {
    w = getb(&src, 9);

    /*printf("oLen: %d _bc: %02X, _bb: %04X, w: %04X\n",oLen,_bc,_bb,w);*/

    if (w < 0x100) {
      assert((intptr_t) dst > (intptr_t) orgdst);
      *--dst = w;
      /*printf("0+[8] -> %02X\n",w);*/
      oLen--;
    } else if (w == 0x13e || w == 0x13f) {
      w <<= 4;
      w |= getb(&src, 4);

      n = (w & 0x1f) + 14;
      oLen -= n;
      while (n-- > 0) {
        w = getb(&src, 8);

        /*printf("1+001+1111+[4] -> [8] -> %02X\n",w);*/
	assert((intptr_t) dst > (intptr_t) orgdst);
        *--dst = w;
      }
    } else {
      if (w >= 0x180) {
        /* copy 2-3 */
        n = w & 0x40 ? 3 : 2;
        
        if (w & 0x20) {
          /* dist 545 -> */
          w = (w & 0x1f) << (eff-5);
          w |= getb(&src, eff-5);
          w += 544;
          /* printf("1+1+[1]+1+[%d] -> ",eff); */
        } else if (w & 0x30) {
          // dist 1 -> 32
          w = (w & 0x0f) << 1;
          w |= getb(&src,1);
          /* printf("1+1+[1]+01+[5] %d %02X %d %04X-> ",n,w, _bc, _bb); */
        } else {
          /* dist 33 -> 544 */
          w = (w & 0x0f) << 5;
          w |= getb(&src, 5);
          w += 32;
          /* printf("1+1+[1]+00+[9] -> "); */
        }
      } else if (w >= 0x140) {
        /* copy 4-7 */
        n = ((w & 0x30) >> 4) + 4;
        
        if (w & 0x08) {
          /* dist 545 -> */
          w = (w & 0x07) << (eff-3);
          w |= getb(&src,eff-3);
          w += 544;
          /* printf("1+01+[2]+1+[%d] -> ",eff); */
        } else if (w & 0x0c) {
          /* dist 1 -> 32 */
          w = (w & 0x03) << 3;
          w |= getb(&src,3);
          /* printf("1+01+[2]+01+[5] -> "); */
        } else {
          /* dist 33 -> 544 */
          w = (w & 0x03) << 7;
          w |= getb(&src,7);
          w += 32;
          /* printf("1+01+[2]+00+[9] -> "); */
        }
      } else if (w >= 0x120) {
        /* copy 8-22 */
        n = ((w & 0x1e) >> 1) + 8;
        
        if (w & 0x01) {
          /* dist 545 -> */
          w = getb(&src,eff);
          w += 544;
          /* printf("1+001+[4]+1+[%d] -> ",eff); */
        } else {
          w = getb(&src,6);

          if (w & 0x20) {
            /* dist 1 -> 32 */
            w &= 0x1f;
            /* printf("1+001+[4]+001+[5] -> "); */
          } else {
            /* dist 33 -> 544 */
            w <<= 4;
            w |= getb(&src,4);

            w += 32;
            /* printf("1+001+[4]+00+[9] -> "); */
          }
        }
      } else {
        w = (w & 0x1f) << 3;
	w |= getb(&src,3);
        n = 23;

        while (w == 0xff) {
          n += w;
          w = getb(&src,8);
        }
        n += w;

        w = getb(&src,7);

        if (w & 0x40) {
          /* dist 545 -> */
          w = (w & 0x3f) << (eff - 6);
          w |= getb(&src,eff - 6);

          w += 544;
        } else if (w & 0x20) {
          /* dist 1 -> 32 */
          w &= 0x1f;
          /* printf("1+000+[8]+01+[5] -> "); */
        } else {
          /* dist 33 -> 544; */
          w <<= 4;
	  w |= getb(&src,4);

          w += 32;
          /* printf("1+000+[8]+00+[9] -> "); */
        }
      }

      /* printf("<%d,%d>\n",n,w+1); fflush(stdout); */
      oLen -= n;

      while (n-- > 0) {
        /* printf("Copying: %02X\n",dst[w]); */
	dst--;
	assert((intptr_t) dst >= (intptr_t) orgdst);
	assert((intptr_t) (dst + w + 1) < (intptr_t) (orgdst + dst_length));
	*dst = dst[w + 1];
      }
    }
  }
}




int main( int argc, char **argv )
{
  int32_t oLen, sLen, pLen;
  uint8_t *src;
  uint8_t *dst;
  size_t n;
  FILE *fh;

  if (argc != 3) {
    fprintf(stderr,"Usage: %s infile outfile\n", argv[0]);
    exit(EXIT_FAILURE);
  }

  if ((fh = fopen(argv[1], "rb")) == NULL) {
    fprintf(stderr,"** Error: fopen(%s) failed..\n",argv[1]);
    exit(EXIT_FAILURE);
  }

  if ((n = checkS404File(fh, &oLen, &pLen, &sLen)) < 0) {
    fprintf(stderr,"** Error: checkS404File() failed..\n");
    fclose(fh);
    exit(EXIT_FAILURE);
  }

  printf("%s is a S404 compressed file\n", argv[1]);
  printf("\tOriginal length: %d\n", oLen);
  printf("\tCompressed length: %d\n", pLen);
  printf("\tSecurity length: %d\n", sLen);
  printf("\tRead length: %zd\n", n);

  if ((src = malloc(n)) == NULL) {
    fprintf(stderr,"** Error: malloc(%zd) failed..\n", n);
    fclose(fh);
    exit(EXIT_FAILURE);
  }
  if ((dst = malloc(oLen)) == NULL) {
    fprintf(stderr,"** Error: malloc(%d) failed..\n",oLen);
    free(src);
    fclose(fh);
    exit(EXIT_FAILURE);
  }
  if (fread(src,1,n,fh) != n) {
    fprintf(stderr,"** Error: fread() failed..\n");
    free(src);
    free(dst);
    fclose(fh);
    exit(EXIT_FAILURE);
  }

  fclose(fh);

  if ((fh = fopen(argv[2], "wb")) == NULL) {
    fprintf(stderr,"** Error: fopen(%s) failed..\n",argv[2]);
    free(src);
    free(dst);
    exit(EXIT_FAILURE);
  }

  decompressS404(src, dst, oLen, pLen);

  if (fwrite(dst, 1, oLen, fh) != oLen) {
    fprintf(stderr,"** Error: fwrite() failed..\n");
    free(src);
    free(dst);
    fclose(fh);
    exit(EXIT_FAILURE);
  }

  printf("Done...\n");

  free(src);
  free(dst);
  fclose(fh);

  return 0;
}
