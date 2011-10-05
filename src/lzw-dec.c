/******************************************************************************
**  LZW decoder
**  --------------------------------------------------------------------------
**  
**  Compresses data using LZW algorithm.
**  
**  Author: V.Antonenko
**
**
******************************************************************************/
#include <stdlib.h>
#include <stdio.h>
#include <memory.h>
#include "lzw.h"

// LZW decoder node, represents a string
typedef struct _node_dec
{
	int	 prev;	// prefix code
	char ch;	// last symbol
}
node_dec_t;

typedef struct _lzw_dec
{
	node_dec_t    dict[DICT_SIZE];	// code dictionary
	unsigned char buff[DICT_SIZE];	// output string buffer
	int           code;				// current code
	unsigned      max;				// maximal code
	unsigned      codesize;			// number of bits in code
	bitbuffer_t   bb;				// bit-buffer struct
	void          *stream;			// pointer to the stream object
	unsigned      lzwn;				// input buffer byte counter
	unsigned      lzwm;				// input buffer size
	unsigned char *inbuff;		    // input code buffer
	unsigned char c;				// first char of the code
}
lzw_dec_t;

/******************************************************************************
**  lzw_dec_readbits
**  --------------------------------------------------------------------------
**  Read bits from bit-buffer.
**  The number of bits should not exceed 24.
**  
**  Arguments:
**      ctx     - pointer to LZW context;
**      nbits   - number of bits to read, 0-24;
**
**  Return: bits
******************************************************************************/
static int lzw_dec_readbits(lzw_dec_t *ctx, unsigned nbits)
{
	unsigned bits;

	// read bytes
	while (ctx->bb.n < nbits)
	{
		if (ctx->lzwn == ctx->lzwm)
			return -1;

		// shift old bits to the left, add new to the right
		ctx->bb.buf = (ctx->bb.buf << 8) | ctx->inbuff[ctx->lzwn++];
		ctx->bb.n += 8;
	}

	ctx->bb.n -= nbits;
	bits = (ctx->bb.buf >> ctx->bb.n) & ((1 << nbits)-1);

	return bits;
}

/******************************************************************************
**  lzw_init
**  --------------------------------------------------------------------------
**  Initializes LZW context.
**  
**  Arguments:
**      ctx     - LZW context;
**      stream  - Pointer to Input/Output stream object;
**
**  RETURN: -
******************************************************************************/
void lzw_dec_init(lzw_dec_t *ctx, void *stream)
{
	unsigned i;

	memset(ctx, 0, sizeof(*ctx));
	
	for (i = 0; i < 256; i++)
	{
		ctx->dict[i].prev  = NODE_NULL;
		ctx->dict[i].ch = i;
	}

	ctx->dict[NODE_NULL].prev  = NODE_NULL;

	ctx->code = NODE_NULL;
	ctx->max = i-1;
	ctx->codesize = 8;
	ctx->stream = stream;
}

static void lzw_dec_reset(lzw_dec_t *ctx)
{
	int nc;

	ctx->max = 255;
	ctx->codesize = 8;

	if ((nc = lzw_dec_readbits(ctx, ctx->codesize)) < 0)
		return;
	
	ctx->codesize++;
	ctx->c = ctx->code = nc;
	// write symbol into the output stream
	lzw_writebuf(ctx->stream, &ctx->c, 1);
}


/******************************************************************************
**  lzw_dec_getstr
**  --------------------------------------------------------------------------
**  Reads string from the LZW dictionaly. Because of particular dictionaty
**  structure the buffer is filled from the end so the offset from the 
**  beginning of the buffer will be <buffer size> - <string size>.
**  
**  Arguments:
**      ctx  - LZW context;
**      code - code of the string (already in dictionary);
**      buff - buffer for the string;
**      size - the buffer size;
**
**  Return: the number of bytes in the string
******************************************************************************/
static unsigned lzw_dec_getstr(lzw_dec_t *ctx, int code)
{
	unsigned i = sizeof(ctx->buff);

	while (code != NODE_NULL && i)
	{
		ctx->buff[--i] = ctx->dict[code].ch;
		code = ctx->dict[code].prev;
	}

	return sizeof(ctx->buff) - i;
}

/******************************************************************************
**  lzw_dec_addstr
**  --------------------------------------------------------------------------
**  Adds string to the LZW dictionaly.
**  It is important that codesize is increased after the code was sent into
**  the output stream.
**  
**  Arguments:
**      ctx  - LZW context;
**      code - code for the string beginning (already in dictionary);
**      c    - last symbol;
**
**  RETURN: code representing the string or NODE_NULL if dictionary is full.
******************************************************************************/
static int lzw_dec_addstr(lzw_dec_t *ctx, int code, char c)
{
	if (ctx->max == NODE_NULL)
		return NODE_NULL;
	
	if (code == NODE_NULL)
		return c;
		
	ctx->max++;

	ctx->dict[ctx->max].prev = code;
	ctx->dict[ctx->max].ch = c;

	return ctx->max;
}

/******************************************************************************
**  lzw_dec_readcode
**  --------------------------------------------------------------------------
**  Reads a code from the input stream. Be careful about where you put its
**  call because this function changes the codesize.
**  This function is used only in decoder.
**  
**  Arguments:
**      ctx  - LZW context;
**
**  RETURN: code
******************************************************************************/
static int lzw_dec_readcode(lzw_dec_t *ctx)
{
	return lzw_dec_readbits(ctx, ctx->codesize);
}

static unsigned char lzw_dec_writestr(lzw_dec_t *ctx, int code)
{
	unsigned strlen;

	if (code == NODE_NULL)
		return 0;

	// get string for the new code from dictionary
	strlen = lzw_dec_getstr(ctx, code);
	// write the string into the output stream
	lzw_writebuf(ctx->stream, ctx->buff+(sizeof(ctx->buff) - strlen), strlen);
	// remember the first sybmol of this string
	return ctx->buff[sizeof(ctx->buff) - strlen];
}

int lzw_decode_buf(lzw_dec_t *ctx, unsigned char buf[], unsigned size)
{
	if (!size) return 0;

	ctx->inbuff = buf;
	ctx->lzwn = 0;
	ctx->lzwm = size;

	for (;;)
	{
		int nc;

		nc = lzw_dec_readcode(ctx);

		// check the input stream for EOF
		if (nc < 0)
		{
			if (ctx->lzwn != ctx->lzwm)
				fprintf(stderr, "ERROR: input buffer %d,%d\n", ctx->lzwn, ctx->lzwm);
			break;
		}
		else if (nc <= ctx->max)
		{
			// output string for the new code from dictionary
			ctx->c = lzw_dec_writestr(ctx, nc);

			// add <prev code str>+<first str symbol> to the dictionary
			if (lzw_dec_addstr(ctx, ctx->code, ctx->c) == NODE_NULL)
			{
				fprintf(stderr, "ERROR: dictionary is full\n");
				break;
			}

			// increase the code size (number of bits) if needed
			if (ctx->max+1 == (1 << ctx->codesize))
				ctx->codesize++;

			if (ctx->max == (DICT_SIZE-1))
				lzw_dec_reset(ctx);
		}
		else // unknown code
		{
			if (nc-1 == ctx->max)
			{
				// create code: <nc> = <code> + <c>
				if (lzw_dec_addstr(ctx, ctx->code, ctx->c) == NODE_NULL)
				{
					fprintf(stderr, "ERROR: dictionary is full\n");
					break;
				}

				// output string for the new code from dictionary
				ctx->c = lzw_dec_writestr(ctx, nc);

				// increase the code size (number of bits) if needed
				if (nc+1 == (1 << ctx->codesize))
					ctx->codesize++;

				if (ctx->max == (DICT_SIZE-1))
					lzw_dec_reset(ctx);
			}
			else {
				fprintf(stderr, "ERROR: wrong code %d\n", nc);
				break;
			}
		}

		ctx->code = nc;
	}

	return ctx->lzwn;
}

/******************************************************************************
**  lzw_decode
**  --------------------------------------------------------------------------
**  Decodes input LZW code stream into byte stream.
**  
**  Arguments:
**      ctx  - LZW context;
**      fin  - input file;
**      fout - output file;
**
**  Return: error code
******************************************************************************/
int lzw_decode(lzw_dec_t *ctx, FILE *fin, FILE *fout)
{
	unsigned      len;
	unsigned char buf[256];

	lzw_dec_init(ctx, fout);

	while (len = lzw_readbuf(fin, buf, sizeof(buf)))
	{
		lzw_decode_buf(ctx, buf, len);
	}

	return 0;
}

// global object
lzw_dec_t lzw;

void lzw_writebuf(void *stream, unsigned char *buf, unsigned size)
{
	fwrite(buf, size, 1, (FILE*)stream);
	fflush((FILE*)stream);
}

unsigned lzw_readbuf(void *stream, unsigned char *buf, unsigned size)
{
	return fread(buf, 1, size, (FILE*)stream);
}

int main (int argc, char* argv[])
{
	FILE *fin, *fout;

	if (argc < 3) {
		printf("Usage: lzw-dec <input file> <output file>\n");
		return -1;
	}

	if (!(fin = fopen(argv[1], "rb"))) {
		fprintf(stderr, "Cannot open %s\n", argv[1]);
		return -2;
	}

	if (!(fout = fopen(argv[2], "w+b"))) {
		fprintf(stderr, "Cannot open %s\n", argv[2]);
		return -3;
	}

	lzw_decode(&lzw, fin, fout);

	fclose(fin);
	fclose(fout);

	return 0;
}