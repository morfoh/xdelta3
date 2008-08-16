/* xdelta 3 - delta compression tools and library
 * Copyright (C) 2007.  Joshua P. MacDonald
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifndef _XDELTA3_MERGE_H_
#define _XDELTA3_MERGE_H_

int xd3_merge_inputs (xd3_stream *stream, 
		      xd3_whole_state *source,
		      xd3_whole_state *input);

static int
xd3_whole_state_init (xd3_stream *stream)
{
  XD3_ASSERT (stream->whole_target.adds == NULL);
  XD3_ASSERT (stream->whole_target.inst == NULL);
  XD3_ASSERT (stream->whole_target.length == 0);

  stream->whole_target.adds_alloc = XD3_ALLOCSIZE;
  stream->whole_target.inst_alloc = XD3_ALLOCSIZE / sizeof (xd3_winst);

  if ((stream->whole_target.adds = (uint8_t*) 
       xd3_alloc (stream, XD3_ALLOCSIZE, 1)) == NULL ||
      (stream->whole_target.inst = (xd3_winst*) 
       xd3_alloc (stream, XD3_ALLOCSIZE, sizeof(xd3_winst))) == NULL)
    {
      return ENOMEM;
    }
  return 0;
}

static void
xd3_swap_whole_state (xd3_whole_state *a, 
		      xd3_whole_state *b)
{
  xd3_whole_state tmp;
  XD3_ASSERT (a->inst != NULL && a->adds != NULL);
  XD3_ASSERT (b->inst != NULL && b->adds != NULL);
  memcpy (&tmp, a, sizeof (xd3_whole_state));
  memcpy (a, b, sizeof (xd3_whole_state));
  memcpy (b, &tmp, sizeof (xd3_whole_state));
}

static int
xd3_realloc_buffer (xd3_stream *stream,
                    usize_t current_units,
                    usize_t unit_size,
                    usize_t new_units,
                    usize_t *alloc_size,
                    void **alloc_ptr)
{
  usize_t needed;
  usize_t new_alloc;
  usize_t cur_size;
  uint8_t *new_buf;

  needed = (current_units + new_units) * unit_size;

  if (needed <= *alloc_size)
    {
      return 0;
    }

  cur_size = current_units * unit_size;
  new_alloc = xd3_round_blksize (needed * 2, XD3_ALLOCSIZE);

  if ((new_buf = (uint8_t*) xd3_alloc (stream, new_alloc, 1)) == NULL)
    {
      return ENOMEM;
    }

  if (cur_size != 0)
    {
      memcpy (new_buf, *alloc_ptr, cur_size);
    }

  if (*alloc_ptr != NULL)
    {
      xd3_free (stream, *alloc_ptr);
    }

  *alloc_size = new_alloc;
  *alloc_ptr = new_buf;

  return 0;
}

/* allocate one new output instruction */
static int
xd3_whole_alloc_winst (xd3_stream *stream,
		       xd3_winst **winstp)
{
  int ret;

  if ((ret = xd3_realloc_buffer (stream, 
				 stream->whole_target.instlen, 
				 sizeof (xd3_winst), 
				 1, 
				 & stream->whole_target.inst_alloc, 
				 (void**) & stream->whole_target.inst))) 
    { 
      return ret; 
    }

  *winstp = &stream->whole_target.inst[stream->whole_target.instlen++];

  return 0;
}

static int
xd3_whole_alloc_adds (xd3_stream *stream,
		      usize_t count)
{
  return xd3_realloc_buffer (stream,
			     stream->whole_target.addslen,
			     1,
			     count,
			     & stream->whole_target.adds_alloc,
			     (void**) & stream->whole_target.adds);
}

static int
xd3_whole_append_inst (xd3_stream *stream,
                       xd3_hinst *inst)
{
  int ret;
  xd3_winst *winst;

  if ((ret = xd3_whole_alloc_winst (stream, &winst)))
    {
      return ret;
    }

  winst->type = inst->type;
  winst->mode = 0;
  winst->size = inst->size;
  winst->position = stream->whole_target.length;
  stream->whole_target.length += inst->size;

  if (((inst->type == XD3_ADD) || (inst->type == XD3_RUN)) &&
      (ret = xd3_whole_alloc_adds (stream, 
				   (inst->type == XD3_RUN ? 1 : inst->size))))
    {
      return ret;
    }

  switch (inst->type)
    {
    case XD3_RUN:
      winst->addr = stream->whole_target.addslen;
      stream->whole_target.adds[stream->whole_target.addslen++] =
        *stream->data_sect.buf++;
      break;

    case XD3_ADD:
      winst->addr = stream->whole_target.addslen;
      memcpy (stream->whole_target.adds + stream->whole_target.addslen,
              stream->data_sect.buf,
              inst->size);
      stream->data_sect.buf += inst->size;
      stream->whole_target.addslen += inst->size;
      break;

    default:
      if (inst->addr < stream->dec_cpylen)
	{
	  winst->mode = SRCORTGT (stream->dec_win_ind);
	  winst->addr = stream->dec_cpyoff + inst->addr;
	}
      else
	{
	  winst->addr = stream->total_out + inst->addr - stream->dec_cpylen;
	}
      break;
    }

  return 0;
}

int
xd3_whole_append_window (xd3_stream *stream)
{
  int ret;

  while (stream->inst_sect.buf < stream->inst_sect.buf_max)
    {
      if ((ret = xd3_decode_instruction (stream)))
	{
	  return ret;
	}

      if ((stream->dec_current1.type != XD3_NOOP) &&
          (ret = xd3_whole_append_inst (stream,
                                        & stream->dec_current1)))
	{
          return ret;
	}

      if ((stream->dec_current2.type != XD3_NOOP) &&
          (ret = xd3_whole_append_inst (stream,
                                        & stream->dec_current2)))
	{
          return ret;
	}
    }

  return 0;
}

/* xd3_merge_input_output applies *source to *stream, returns the
 * result in stream. */
int xd3_merge_input_output (xd3_stream *stream,
			    xd3_whole_state *source)
{
  int ret;
  xd3_stream tmp_stream;
  memset (& tmp_stream, 0, sizeof (tmp_stream));
  if ((ret = xd3_config_stream (& tmp_stream, NULL)) ||
      (ret = xd3_whole_state_init (& tmp_stream)) ||
      (ret = xd3_merge_inputs (& tmp_stream, 
			       source,
			       & stream->whole_target)))
    {
      XPR(NT XD3_LIB_ERRMSG (&tmp_stream, ret));
      return ret;
    }

  /* the output is in tmp_stream.whole_state, swap into input */
  xd3_swap_whole_state (& stream->whole_target,
			& tmp_stream.whole_target);
  /* total allocation counts are preserved */
  xd3_free_stream (& tmp_stream);
  return 0;
}

static int
xd3_merge_run (xd3_stream *stream,
	       xd3_whole_state *target,
	       xd3_winst *iinst)
{
  int ret;
  xd3_winst *oinst;

  if ((ret = xd3_whole_alloc_winst (stream, &oinst)) ||
      (ret = xd3_whole_alloc_adds (stream, 1)))
    {
      return ret;
    }

  oinst->type = iinst->type;
  oinst->mode = iinst->mode;
  oinst->size = iinst->size;
  oinst->addr = stream->whole_target.addslen;

  XD3_ASSERT (stream->whole_target.length == iinst->position);
  oinst->position = stream->whole_target.length;
  stream->whole_target.length += iinst->size;

  stream->whole_target.adds[stream->whole_target.addslen++] = 
    target->adds[iinst->addr];

  return 0;
}

static int
xd3_merge_add (xd3_stream *stream,
	       xd3_whole_state *target,
	       xd3_winst *iinst)
{
  int ret;
  xd3_winst *oinst;

  if ((ret = xd3_whole_alloc_winst (stream, &oinst)) ||
      (ret = xd3_whole_alloc_adds (stream, iinst->size)))
    {
      return ret;
    }

  oinst->type = iinst->type;
  oinst->mode = iinst->mode;
  oinst->size = iinst->size;
  oinst->addr = stream->whole_target.addslen;

  XD3_ASSERT (stream->whole_target.length == iinst->position);
  oinst->position = stream->whole_target.length;
  stream->whole_target.length += iinst->size;

  memcpy(stream->whole_target.adds + stream->whole_target.addslen,
	 target->adds + iinst->addr,
	 iinst->size);

  stream->whole_target.addslen += iinst->size;

  return 0;
}

static int
xd3_merge_target_copy (xd3_stream *stream,
		       xd3_winst *iinst)
{
  int ret;
  xd3_winst *oinst;

  // TODO: this is totally untested

  if ((ret = xd3_whole_alloc_winst (stream, &oinst)))
    {
      return ret;
    }

  XD3_ASSERT (stream->whole_target.length == iinst->position);
  stream->whole_target.length += iinst->size;

  memcpy (oinst, iinst, sizeof (*oinst));
  return 0;
}

static int
xd3_merge_find_position (xd3_stream *stream,
			 xd3_whole_state *source,
			 xoff_t address,
			 usize_t *inst_num)
{
  usize_t low;
  usize_t high;

  if (address >= source->length)
    {
      stream->msg = "Invalid copy offset in merge";
      return XD3_INVALID_INPUT;
    }

  low = 0;
  high = source->instlen;

  while (low != high)
    {
      xoff_t mid_lpos;
      xoff_t mid_hpos;
      usize_t mid = low + (high - low) / 2;
      mid_lpos = source->inst[mid].position;

      if (address < mid_lpos)
	{
	  high = mid;
	  continue;
	}
      
      mid_hpos = mid_lpos + source->inst[mid].size;

      if (address >= mid_hpos)
	{
	  low = mid + 1;
	  continue;
	}

      *inst_num = mid;
      return 0;
    }

  stream->msg = "Internal error in merge";
  return XD3_INTERNAL;
}

static int
xd3_merge_source_copy (xd3_stream *stream,
		       xd3_whole_state *source,
		       xd3_winst *iinst_orig)
{
  int ret;
  xd3_winst iinst;
  usize_t sinst_num;

  memcpy (& iinst, iinst_orig, sizeof (iinst));

  XD3_ASSERT (iinst.mode == VCD_SOURCE);

  if ((ret = xd3_merge_find_position (stream, source, 
				      iinst.addr, &sinst_num)))
    {
      return ret;
    }

  while (iinst.size > 0)
    {
      xd3_winst *sinst;
      xd3_winst *minst;
      usize_t sinst_offset;
      usize_t sinst_left;
      usize_t this_take;

      XD3_ASSERT (sinst_num < source->instlen);

      sinst = &source->inst[sinst_num];

      XD3_ASSERT (iinst.addr >= sinst->position);

      sinst_offset = iinst.addr - sinst->position;

      XD3_ASSERT (sinst->size > sinst_offset);

      sinst_left = sinst->size - sinst_offset;
      this_take = min (iinst.size, sinst_left);

      XD3_ASSERT (this_take > 0);

      if ((ret = xd3_whole_alloc_winst (stream, &minst)))
	{
	  return ret;
	}

      minst->size = this_take;
      minst->type = sinst->type;
      minst->position = iinst.position;
      minst->mode = 0;

      switch (sinst->type)
	{
	case XD3_RUN:
	  if ((ret = xd3_whole_alloc_adds (stream, 1)))
	    {
	      return ret;
	    }

	  minst->addr = stream->whole_target.addslen;
	  stream->whole_target.adds[stream->whole_target.addslen++] = 
	    source->adds[sinst->addr];
	  break;
	case XD3_ADD:
	  if ((ret = xd3_whole_alloc_adds (stream, this_take)))
	    {
	      return ret;
	    }

	  minst->addr = stream->whole_target.addslen;
	  memcpy(stream->whole_target.adds + stream->whole_target.addslen,
		 source->adds + sinst->addr + sinst_offset,
		 this_take);
	  stream->whole_target.addslen += this_take;
	  break;
	default:
	  minst->mode = VCD_SOURCE;
	  minst->addr = sinst->addr + sinst_offset;
	  break;
	}

      stream->whole_target.length += this_take;
      iinst.position += this_take;
      iinst.addr += this_take;
      iinst.size -= this_take;
      sinst_num += 1;
    }

  return 0;
}

/* xd3_merge_inputs() applies *input to *source, returns its result in
 * stream. */
int xd3_merge_inputs (xd3_stream *stream, 
		      xd3_whole_state *source,
		      xd3_whole_state *input)
{
  int ret = 0;
  size_t input_i;

  /* iterate over each instruction. */
  for (input_i = 0; ret == 0 && input_i < input->instlen; ++input_i)
    {
      xd3_winst *iinst = &input->inst[input_i];

      switch (iinst->type)
	{
	case XD3_RUN:
	  ret = xd3_merge_run (stream, input, iinst);
	  break;
	case XD3_ADD:
	  ret = xd3_merge_add (stream, input, iinst);
	  break;
	default:
	  /* Note: VCD_TARGET support is completely untested all 
	   * throughout. */
	  if (iinst->mode == 0 || iinst->mode == VCD_TARGET)
	    {
	      ret = xd3_merge_target_copy (stream, iinst);
	    }
	  else
	    {
	      ret = xd3_merge_source_copy (stream, source, iinst);
	    }
	  break;
	}
    }
  
  return ret;
}

#endif
