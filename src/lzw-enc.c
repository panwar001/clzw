/******************************************************************************
**  LZW encoder
**  --------------------------------------------------------------------------
**  
**  Compresses data using LZW algorithm.
**  
**  Author: V.Antonenko
**
******************************************************************************/
#include "lzw.h"


/******************************************************************************
**  lzw_enc_writebits
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
static void lzw_enc_writebits(lzw_enc_t *ctx, unsigned bits, unsigned nbits)
{
	// shift old bits to the left, add new to the right
	ctx->bb.buf = (ctx->bb.buf << nbits) | (bits & ((1 << nbits)-1));

	nbits += ctx->bb.n;

	// flush whole bytes
	while (nbits >= 8)
	{
		nbits -= 8;
		ctx->buff[ctx->lzwn++] = ctx->bb.buf >> nbits;

		if (ctx->lzwn == sizeof(ctx->buff)) {
			ctx->lzwn = 0;
			lzw_writebuf(ctx->stream, ctx->buff, sizeof(ctx->buff));
		}
	}

	ctx->bb.n = nbits;
}

/******************************************************************************
**  lzw_enc_init
**  --------------------------------------------------------------------------
**  Initializes LZW encoder context.
**  
**  Arguments:
**      ctx     - LZW context;
**      stream  - Pointer to Input/Output stream object;
**
**  Return: -
******************************************************************************/
void lzw_enc_init(lzw_enc_t *ctx, void *stream)
{
	unsigned i;

	for (i = 0; i < 256; i++)
	{
		ctx->dict[i].prev  = NODE_NULL-1;
		ctx->dict[i].first = NODE_NULL;
		ctx->dict[i].next  = i+1;
		ctx->dict[i].ch = i;
	}
	ctx->dict[i-1].next = NODE_NULL;

	ctx->dict[NODE_NULL-1].prev  = NODE_NULL;
	ctx->dict[NODE_NULL-1].first = 0;
	ctx->dict[NODE_NULL-1].next  = NODE_NULL;

	ctx->code = NODE_NULL-1;
	ctx->max = i-1;
	ctx->codesize = 8;
	ctx->stream = stream;
}

/******************************************************************************
**  lzw_enc_reset
**  --------------------------------------------------------------------------
**  Reset LZW encoder context. Used when the dictionary overflows.
**  Code size set to 8 bit.
**  
**  Arguments:
**      ctx     - LZW encoder context;
**
**  Return: -
******************************************************************************/
static void lzw_enc_reset(lzw_enc_t *ctx)
{
	unsigned i;

#if DEBUG
	printf("reset\n");
#endif

	for (i = 0; i < 256; i++)
	{
		ctx->dict[i].first = NODE_NULL;
	}

	ctx->max = i-1;
	ctx->codesize = 8;
}

/******************************************************************************
**  lzw_enc_findstr
**  --------------------------------------------------------------------------
**  Searches a string in LZW dictionaly. It is used only in encoder.
**  Fast search is performed using embedded linked lists.
**  
**  Arguments:
**      ctx  - LZW context;
**      code - code for the string beginning (already in dictionary);
**      c    - last symbol;
**
**  Return: code representing the string or NODE_NULL.
******************************************************************************/
static code_t lzw_enc_findstr(lzw_enc_t *ctx, code_t code, char c)
{
	code_t nc;

	for (nc = ctx->dict[code].first; nc != NODE_NULL; nc = ctx->dict[nc].next)
	{
		if (code == ctx->dict[nc].prev && c == ctx->dict[nc].ch)
			return nc;
	}

	return NODE_NULL;
}

/******************************************************************************
**  lzw_enc_addstr
**  --------------------------------------------------------------------------
**  Adds string to the LZW dictionaly.
**  
**  Arguments:
**      ctx  - LZW context;
**      code - code for the string beginning (already in dictionary);
**      c    - last symbol;
**
**  Return: code representing the string or NODE_NULL if dictionary is full.
******************************************************************************/
static code_t lzw_enc_addstr(lzw_enc_t *ctx, code_t code, char c)
{
	if (ctx->max == NODE_NULL || code == NODE_NULL)
		return NODE_NULL;
	
	ctx->max++;

	ctx->dict[ctx->max].prev = code;
	ctx->dict[ctx->max].first = NODE_NULL;
	ctx->dict[ctx->max].next = ctx->dict[code].first;
	ctx->dict[code].first = ctx->max;
	ctx->dict[ctx->max].ch = c;
#if DEBUG
	printf("add code %x = %x + %c\n", ctx->max, code, c);
#endif

	return ctx->max;
}

/******************************************************************************
**  lzw_encode_buf
**  --------------------------------------------------------------------------
**  Encode buffer by LZW algorithm. The output data is written by application
**  specific callback to the application defined stream inside this function.
**  
**  Arguments:
**      ctx  - LZW encoder context;
**      buf  - input byte buffer;
**      size - size of the buffer;
**
**  Return: Number of processed bytes.
******************************************************************************/
int lzw_encode_buf(lzw_enc_t *ctx, unsigned char buf[], unsigned size)
{
	unsigned i;

	if (!size) return 0;

	for (i = 0; i < size; i++)
	{
		unsigned char c = buf[i];
		code_t        nc = lzw_enc_findstr(ctx, ctx->code, c);

		if (nc == NODE_NULL)
		{
			// the string was not found - write <prefix>
			lzw_enc_writebits(ctx, ctx->code, ctx->codesize);
#if DEBUG
			printf("code %x (%d)\n", ctx->code, ctx->codesize);
#endif
			// increase the code size (number of bits) if needed
			if (ctx->max+1 == (1 << ctx->codesize))
				ctx->codesize++;

			// add <prefix>+<current symbol> to the dictionary
			if (lzw_enc_addstr(ctx, ctx->code, c) == NODE_NULL)
			{
				// dictionary is full - reset encoder
				lzw_enc_reset(ctx);
			}

			ctx->code = c;
		}
		else
		{
			ctx->code = nc;
		}
	}

	return size;
}

/******************************************************************************
**  lzw_encode_end
**  --------------------------------------------------------------------------
**  Finish LZW encoding process. As output data is written into output stream
**  via bit-buffer it can contain unsaved data. This function flushes
**  bit-buffer and padds last byte with zero bits.
**  
**  Arguments:
**      ctx  - LZW encoder context;
**
**  Return: -
******************************************************************************/
void lzw_encode_end(lzw_enc_t *ctx)
{
#if DEBUG
	printf("code %x (%d)\n", ctx->code, ctx->codesize);
#endif
	// write last code
	lzw_enc_writebits(ctx, ctx->code, ctx->codesize);
	// flush bits in the bit-buffer
	lzw_enc_writebits(ctx, 0, 8 - ctx->bb.n);
	lzw_writebuf(ctx->stream, ctx->buff, ctx->lzwn);
}