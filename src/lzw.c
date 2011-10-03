/******************************************************************************
**  LZW compression
**  --------------------------------------------------------------------------
**  
**  Compresses data using LZW algorithm.
**  
**  Parameters: input byte stream.
**
**  Return: A lot of files, each file is a H.264 NAL unit.
******************************************************************************/
#include <stdlib.h>
#include <stdio.h>
#include <memory.h>

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
	code_t	prev;	// prefix code
	char	ch;		// last symbol
}
node_t;

// LZW context
typedef struct _lzw
{
	node_t        dict[DICT_SIZE];	// code dictionary
	unsigned      max;				// maximal code
	unsigned      codesize;			// number of bits in code
	bitbuffer_t   bb;				// bit-buffer struct
	void          *stream;			// pointer to the stream object
	unsigned      lzwn;				// buffer byte counter
	unsigned      lzwm;				// buffer size (decoder only)
	unsigned char buff[256];		// stream buffer, adjust its size if you need
}
lzw_t;

// Application defined stream callbacks
void lzw_writebuf(void *stream, unsigned char *buf, unsigned size);
unsigned lzw_readbuf(void *stream, unsigned char *buf, unsigned size);

static void lzw_writebyte(lzw_t *ctx, const unsigned char b)
{
	ctx->buff[ctx->lzwn++] = b;

	if (ctx->lzwn == sizeof(ctx->buff)) {
		ctx->lzwn = 0;
		lzw_writebuf(ctx->stream, ctx->buff, sizeof(ctx->buff));
	}
}

static unsigned char lzw_readbyte(lzw_t *ctx)
{
	if (ctx->lzwn == ctx->lzwm)
	{
		ctx->lzwm = lzw_readbuf(ctx->stream, ctx->buff, sizeof(ctx->buff));
		ctx->lzwn = 0;
	}

	return ctx->buff[ctx->lzwn++];
}

/******************************************************************************
**  lzw_writebits
**  --------------------------------------------------------------------------
**  Write bits into bit-buffer.
**  The number of bits should not exceed 24.
**  
**  Arguments:
**      ctx     - pointer to LZW context;
**      bits    - bits to write;
**      nbits   - number of bits to write, 0-24;
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

/******************************************************************************
**  lzw_readbits
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
static unsigned lzw_readbits(lzw_t *ctx, unsigned nbits)
{
	unsigned bits;

	// read bytes
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
void lzw_init(lzw_t *ctx, void *stream)
{
	unsigned i;

	memset(ctx, 0, sizeof(*ctx));
	
	for (i = 0; i < 256; i++)
	{
		ctx->dict[i].prev = NODE_NULL;
		ctx->dict[i].ch = i;
	}

	ctx->max = i-1;
	ctx->codesize = 8;
	ctx->stream = stream;
}

/******************************************************************************
**  lzw_find_str
**  --------------------------------------------------------------------------
**  Searches a string in LZW dictionaly.
**  
**  Arguments:
**      ctx  - LZW context;
**      code - code for the string beginning (already in dictionary);
**      c    - last symbol;
**
**  RETURN: code representing the string or NODE_NULL.
******************************************************************************/
static code_t lzw_find_str(lzw_t *ctx, code_t code, char c)
{
	code_t nc; 

	for (nc = code + 1; nc <= ctx->max; nc++)
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
static unsigned lzw_get_str(lzw_t *ctx, code_t code, unsigned char buff[], unsigned size)
{
	unsigned i = size;

	while (code != NODE_NULL && i)
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
static code_t lzw_add_str(lzw_t *ctx, code_t code, char c)
{
	ctx->max++;

	if (ctx->max >= DICT_SIZE)
		return NODE_NULL;
	
	ctx->dict[ctx->max].prev = code;
	ctx->dict[ctx->max].ch = c;

	return ctx->max;
}

/******************************************************************************
**  lzw_write
**  --------------------------------------------------------------------------
**  Writes an output code into the stream.
**  This function is used only in encoder.
**  
**  Arguments:
**      ctx  - LZW context;
**      code - code for the string;
**
**  RETURN: -
******************************************************************************/
static void lzw_write(lzw_t *ctx, code_t code)
{
	// increase the code size (number of bits) if needed
	if (ctx->max == (1 << ctx->codesize))
		ctx->codesize++;

	lzw_writebits(ctx, code, ctx->codesize);
}

/******************************************************************************
**  lzw_read
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
static code_t lzw_read(lzw_t *ctx)
{
	// increase the code size (number of bits) if needed
	if (ctx->max+1 == (1 << ctx->codesize))
		ctx->codesize++;

	return lzw_readbits(ctx, ctx->codesize);
}

/******************************************************************************
**  lzw_encode
**  --------------------------------------------------------------------------
**  Encodes input byte stream into LZW code stream.
**  
**  Arguments:
**      ctx  - LZW context;
**      fin  - input file;
**      fout - output file;
**
**  Return: error code
******************************************************************************/
int lzw_encode(lzw_t *ctx, FILE *fin, FILE *fout)
{
	code_t   code = NODE_NULL;
	unsigned isize = 0;
	unsigned strlen = 0;
	int      c;

	lzw_init(ctx, fout);

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
	lzw_writebuf(ctx->stream, ctx->buff, ctx->lzwn);

	return 0;
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
int lzw_decode(lzw_t *ctx, FILE *fin, FILE *fout)
{
	unsigned      isize = 0;
	code_t        code;
	unsigned char c;
	unsigned char buff[DICT_SIZE];

	lzw_init(ctx, fin);

	c = code = lzw_readbits(ctx, ctx->codesize++);
	// write symbol into the output stream
	fwrite(&c, 1, 1, fout);

	for(;;)
	{
		unsigned      strlen;
		code_t        nc;

		nc = lzw_read(ctx);

		// check input strean for EOF (lzwm == 0)
		if (!ctx->lzwm)
			break;

		// unknown code
		if (nc > ctx->max)
		{
			if (nc-1 == ctx->max) {
				//fprintf(stderr, "Create code %d = %d + %c\n", nc, code, c);
				if (lzw_add_str(ctx, code, c) == NODE_NULL) {
					fprintf(stderr, "ERROR: dictionary is full, input %d\n", isize);
					break;
				}
				code = NODE_NULL;
			}
			else {
				fprintf(stderr, "ERROR: wrong code %d, input %d\n", nc, isize);
				break;
			}
		}

		// get string for the new code from dictionary
		strlen = lzw_get_str(ctx, nc, buff, sizeof(buff));
		// remember first sybmol in the added string
		c = buff[sizeof(buff) - strlen];
		// write string into the output stream
		fwrite(buff+(sizeof(buff) - strlen), strlen, 1, fout);

		if (code != NODE_NULL)
		{
			// add <prev code str>+<first str symbol> to the dictionary
			if (lzw_add_str(ctx, code, c) == NODE_NULL) {
				fprintf(stderr, "ERROR: dictionary is full, input %d\n", isize);
				break;
			}
		}

		code = nc;
		isize++;
	}

	return 0;
}

// global object
lzw_t lzw;

void lzw_writebuf(void *stream, unsigned char *buf, unsigned size)
{
	fwrite(buf, size, 1, (FILE*)stream);
}

unsigned lzw_readbuf(void *stream, unsigned char *buf, unsigned size)
{
	return fread(buf, 1, size, (FILE*)stream);
}

int main (int argc, char* argv[])
{
	FILE *fin, *fout;

	if (argc < 4) {
		printf("Usage: lzw [e|d] <input file> <output file>\n");
		return -1;
	}

	if (!(fin = fopen(argv[2], "rb"))) {
		fprintf(stderr, "Cannot open %s\n", argv[1]);
		return -2;
	}

	if (!(fout = fopen(argv[3], "w+b"))) {
		fprintf(stderr, "Cannot open %s\n", argv[2]);
		return -3;
	}

	if (argv[1][0] == 'e')
		lzw_encode(&lzw, fin, fout);
	else
		lzw_decode(&lzw, fin, fout);

	fclose(fin);
	fclose(fout);

	return 0;
}