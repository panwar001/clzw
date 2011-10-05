/******************************************************************************
**  LZW decoder
**  --------------------------------------------------------------------------
**  
**  Compresses data using LZW algorithm.
**  
**  Author: V.Antonenko
**
******************************************************************************/
#include "lzw.h"


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
**  lzw_dec_init
**  --------------------------------------------------------------------------
**  Initializes LZW decoder context.
**  
**  Arguments:
**      ctx     - LZW decoder context;
**      stream  - Pointer to application defined Input/Output stream object;
**
**  RETURN: -
******************************************************************************/
void lzw_dec_init(lzw_dec_t *ctx, void *stream)
{
	unsigned i;

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

/******************************************************************************
**  lzw_dec_reset
**  --------------------------------------------------------------------------
**  Reset LZW decoder context. Used when the dictionary overflows.
**  Code size set to 8 bit. Code and output str are equal in this situation.
**  
**  Arguments:
**      ctx     - LZW decoder context;
**
**  RETURN: -
******************************************************************************/
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

/******************************************************************************
**  lzw_dec_writestr
**  --------------------------------------------------------------------------
**  Writes a string represented by the code into output stream.
**  The code should always be in the dictionary.
**  It is important that codesize is increased after code is sent into
**  output stream.
**  
**  Arguments:
**      ctx  - LZW context;
**      code - LZW code;
**
**  RETURN: The first symbol of the output string.
******************************************************************************/
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

/******************************************************************************
**  lzw_decode_buf
**  --------------------------------------------------------------------------
**  Decodes buffer of LZW codes and writes strings into output stream.
**  The output data is written by application specific callback to
**  the application defined stream inside this function.
**  
**  Arguments:
**      ctx  - LZW context;
**      buf  - input code buffer;
**      size - size of the buffer;
**
**  Return: Number of processed bytes or error code if the value is negative.
******************************************************************************/
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
				return LZW_ERR_INPUT_BUF;

			break;
		}
		else if (nc <= ctx->max)
		{
			// output string for the new code from dictionary
			ctx->c = lzw_dec_writestr(ctx, nc);

			// add <prev code str>+<first str symbol> to the dictionary
			if (lzw_dec_addstr(ctx, ctx->code, ctx->c) == NODE_NULL)
				return LZW_ERR_DICT_IS_FULL;

			// increase the code size (number of bits) if needed
			if (ctx->max+1 == (1 << ctx->codesize))
				ctx->codesize++;

			// check dictionary overflow
			if (ctx->max == (DICT_SIZE-1))
				lzw_dec_reset(ctx);
		}
		else // unknown code
		{
			// try to guess the code
			if (nc-1 == ctx->max)
			{
				// create code: <nc> = <code> + <c>
				if (lzw_dec_addstr(ctx, ctx->code, ctx->c) == NODE_NULL)
					return LZW_ERR_DICT_IS_FULL;

				// output string for the new code from dictionary
				ctx->c = lzw_dec_writestr(ctx, nc);

				// increase the code size (number of bits) if needed
				if (nc+1 == (1 << ctx->codesize))
					ctx->codesize++;

				// check dictionary overflow
				if (ctx->max == (DICT_SIZE-1))
					lzw_dec_reset(ctx);
			}
			else
				return LZW_ERR_WRONG_CODE;
		}

		ctx->code = nc;
	}

	return ctx->lzwn;
}
