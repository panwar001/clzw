/******************************************************************************
**  LZW compression
**  --------------------------------------------------------------------------
**  
**  Compresses data using LZW algorithm.
**  
**  Parameters: input byte stream.
**
**
******************************************************************************/
#include <stdlib.h>
#include <stdio.h>
#include <memory.h>
#include <windows.h>

#define DICT_SIZE	(1 << (sizeof(code_t)*8))
#define NODE_NULL	(DICT_SIZE-1)

typedef unsigned short code_t;

typedef struct _bitbuffer
{
	unsigned buf;
	unsigned n;
}
bitbuffer_t;

// LZW node, represents a string
typedef struct _node
{
	code_t	prev;
	char	ch;
}
node_t;

// LZW context
typedef struct _lzw
{
	node_t        dict[DICT_SIZE];
	unsigned      max;
	unsigned      codesize;
	bitbuffer_t   bb;
	void          *file;
	unsigned      lzwn;			// buffer byte counter
	unsigned      lzwm;			// buffer size (decoder only)
	unsigned char buff[256];	// code-stream output buffer, adjust its size if you need
}
lzw_t;

static void lzw_writebuf(lzw_t *ctx, unsigned char *buf, unsigned size)
{
	fwrite(buf, size, 1, (FILE*)ctx->file);
}

static unsigned lzw_readbuf(lzw_t *ctx, unsigned char *buf, unsigned size)
{
	return fread(buf, 1, size, (FILE*)ctx->file);
}

static void lzw_writebyte(lzw_t *ctx, const unsigned char b)
{
	ctx->buff[ctx->lzwn++] = b;

	if (ctx->lzwn == sizeof(ctx->buff)) {
		ctx->lzwn = 0;
		lzw_writebuf(ctx, ctx->buff, sizeof(ctx->buff));
	}
}

static unsigned char lzw_readbyte(lzw_t *ctx)
{
	if (ctx->lzwn == ctx->lzwm)
	{
		ctx->lzwm = lzw_readbuf(ctx, ctx->buff, sizeof(ctx->buff));
		ctx->lzwn = 0;
	}

	return ctx->buff[ctx->lzwn++];
}

/******************************************************************************
**  lzw_writebits
**  --------------------------------------------------------------------------
**  Write bits into bit-buffer.
**  If the number of bits exceeds 16 the result is unpredictable.
**  
**  Arguments:
**      pbb     - pointer to bit-buffer context;
**      bits    - bits to write;
**      nbits   - number of bits to write, 0-16;
**
**  Return: -
******************************************************************************/
static void lzw_writebits(lzw_t *ctx, unsigned bits, unsigned nbits)
{
	// shift old bits to the left, add new to the right
	ctx->bb.buf = (ctx->bb.buf << nbits) | (bits & ((1 << nbits)-1));

	nbits += ctx->bb.n;

	// flush whole bytes
	while (nbits >= 8) {
		unsigned char b;

		nbits -= 8;
		b = ctx->bb.buf >> nbits;

		lzw_writebyte(ctx, b);
	}

	ctx->bb.n = nbits;
}

static unsigned lzw_readbits(lzw_t *ctx, unsigned nbits)
{
	unsigned bits;

	// flush whole bytes
	while (ctx->bb.n < nbits) {
		unsigned char b = lzw_readbyte(ctx);

		// shift old bits to the left, add new to the right
		ctx->bb.buf = (ctx->bb.buf << 8) | b;
		ctx->bb.n += 8;
	}

	ctx->bb.n -= nbits;
	bits = (ctx->bb.buf >> ctx->bb.n) & ((1 << nbits)-1);

	return bits;
}

/******************************************************************************
**  lzw_flushbits
**  --------------------------------------------------------------------------
**  Flush bits into bit-buffer.
**  If there is not an integer number of bytes in bit-buffer - add zero bits
**  and write these bytes.
**  
**  Arguments:
**      pbb     - pointer to bit-buffer context;
**
**  Return: -
******************************************************************************/
static void lzw_flushbits(lzw_t *ctx)
{
	if (ctx->bb.n & 3)
		lzw_writebits(ctx, 0, 8-(ctx->bb.n & 3));
}


// global object
lzw_t lzw;

/******************************************************************************
**  lzw_init
**  --------------------------------------------------------------------------
**  Initializes LZW context.
**  
**  Arguments:
**      ctx  - LZW context;
**
**  RETURN: -
******************************************************************************/
void lzw_init(lzw_t *ctx)
{
	unsigned i;

	memset(ctx, 0, sizeof(*ctx));
	
	for (i = 0; i < 256; i++)
	{
		ctx->dict[i].prev = NODE_NULL;
		ctx->dict[i].ch = i;
	}

	ctx->max = i;
	ctx->codesize = 8;
}

/******************************************************************************
**  lzw_find_str
**  --------------------------------------------------------------------------
**  Finds a string in LZW dictionaly.
**  
**  Arguments:
**      ctx  - LZW context;
**      code - code for the string beginning (already in dictionary);
**      c    - last symbol;
**
**  RETURN: code representing the string or NODE_NULL.
******************************************************************************/
code_t lzw_find_str(lzw_t *ctx, code_t code, char c)
{
	code_t nc; 

	for (nc = code + 1; nc < ctx->max; nc++)
	{
		if (code == ctx->dict[nc].prev && c == ctx->dict[nc].ch)
			return nc;
	}

	return NODE_NULL;
}

/******************************************************************************
**  lzw_get_str
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
unsigned lzw_get_str(lzw_t *ctx, code_t code, unsigned char buff[], unsigned size)
{
	unsigned i = size;

	while (code != NODE_NULL)
	{
		buff[--i] = ctx->dict[code].ch;
		code = ctx->dict[code].prev;
	}

	return size - i;
}

/******************************************************************************
**  lzw_add_str
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
code_t lzw_add_str(lzw_t *ctx, code_t code, char c)
{
	unsigned i = ctx->max++;

	ctx->dict[i].prev = code;
	ctx->dict[i].ch = c;

	//lzw_print_str(ctx, i);

	if (ctx->max >= DICT_SIZE)
	{
		i = NODE_NULL;
	}
	
	return i;
}

/******************************************************************************
**  lzw_write
**  --------------------------------------------------------------------------
**  Writes an output code into the stream.
**  
**  Arguments:
**      ctx  - LZW context;
**      code - code for the string;
**
**  RETURN: -
******************************************************************************/
static void lzw_write(lzw_t *ctx, code_t code)
{
	lzw_writebits(ctx, code, ctx->codesize);
}

static code_t lzw_read(lzw_t *ctx)
{
	return lzw_readbits(ctx, ctx->codesize);
}

int lzw_encode(lzw_t *ctx, FILE *fin, FILE *fout)
{
	code_t   code = NODE_NULL;
	unsigned isize = 0;
	unsigned strlen = 0;
	int      c;

	lzw_init(ctx);

	lzw.file = fout;

	while ((c = fgetc(fin)) != EOF)
	{
		code_t nc;

		isize++;

		nc = lzw_find_str(ctx, code, c);

		if (nc == NODE_NULL)
		{
			code_t tmp;

			// the string was not found - write <prefix>
			lzw_write(ctx, code);

			// add <prefix>+<current symbol> to the dictionary
			tmp = lzw_add_str(ctx, code, c);

			// increase the code size (number of bits) if needed
			if (ctx->max > (1 << ctx->codesize))
				ctx->codesize++;

			if (tmp == NODE_NULL) {
				fprintf(stderr, "ERROR: dictionary is full, input %d\n", isize);
				break;
			}

			code = c;
			strlen = 1;
		}
		else
		{
			code = nc;
			strlen++;
		}
	}
	// write last code
	lzw_write(ctx, code);
	lzw_flushbits(ctx);
	lzw_writebuf(ctx, ctx->buff, ctx->lzwn);

	return 0;
}

int lzw_decode(lzw_t *ctx, FILE *fin, FILE *fout)
{
	code_t        code = NODE_NULL;
	unsigned      isize = 0;
	unsigned char c = 0;
	unsigned char buff[65535];

	lzw_init(ctx);

	lzw.file = fin;

	for(;;)
	{
		unsigned      strlen;
		code_t        nc;

		nc = lzw_read(ctx);
//printf("Code %d\n", nc);
		if (!ctx->lzwm)
			break;

		if (nc > ctx->max) {
			fprintf(stderr, "ERROR: wrong code %d, input %d\n", nc, isize);
			break;
		}

		if (nc == ctx->max) {
			fprintf(stderr, "Create code %d = %d + %c\n", nc, code, c);
			lzw_add_str(ctx, code, c);
			// increase the code size (number of bits) if needed
			if (ctx->max == (1 << ctx->codesize))
				ctx->codesize++;
			code = NODE_NULL;
		}

		// get string for the new code from dictionary
		strlen = lzw_get_str(ctx, nc, buff, sizeof(buff));
		// write string into the output stream
		fwrite(buff+(sizeof(buff) - strlen), strlen, 1, fout);
//fwrite(buff+(sizeof(buff) - strlen), strlen, 1, stdout); printf(":%d\n", strlen);
		// remember first sybmol in the added string
		c = buff[sizeof(buff) - strlen];

		if (code != NODE_NULL)
		{
			// add <prev code str>+<first str symbol> to the dictionary
			if (lzw_add_str(ctx, code, c) == NODE_NULL) {
				fprintf(stderr, "ERROR: dictionary is full, input %d\n", isize);
				break;
			}
			// increase the code size (number of bits) if needed
			if (ctx->max == (1 << ctx->codesize))
				ctx->codesize++;
		}
		else if (isize == 0)
			ctx->codesize++;

		code = nc;
		isize++;
	}

	return 0;
}

int main (int argc, char* argv[])
{
	FILE *fin, *fout;

	if (!(fin = fopen(argv[2], "rb"))) {
		fprintf(stderr, "Cannot open %s\n", argv[1]);
		return -1;
	}

	if (!(fout = fopen(argv[3], "w+b"))) {
		fprintf(stderr, "Cannot open %s\n", argv[2]);
		return -2;
	}

	if (argv[1][0] == 'e')
		lzw_encode(&lzw, fin, fout);
	else
		lzw_decode(&lzw, fin, fout);

	fclose(fin);
	fclose(fout);

	return 0;
}