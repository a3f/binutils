#define UNDERSCORE_HACK 1
#define offsetof(type, identifier) (size_t) &(((type *) 0)->identifier) 
/*
   
 bfd backend for oasys objects.


  Written by Steve Chamberlain
  steve@cygnus.com

 $Id$


 */


#include <ansidecl.h>
#include "sysdep.h"
#include "bfd.h"
#include "libbfd.h"
#include "obstack.h"
#include "oasys.h"
#include "liboasys.h"



#define obstack_chunk_alloc malloc
#define obstack_chunk_free free

#define oasys_malloc(abfd,size) \
  obstack_alloc( &( oasys_data(abfd)->oasys_obstack), (size))

typedef void generic_symbol_type;


static void 
DEFUN(oasys_read_record,(abfd, record),
      bfd *CONST abfd AND 
      oasys_record_union_type *record)
{

  bfd_read(record, 1, sizeof(record->header), abfd);

  bfd_read(((char *)record )+ sizeof(record->header),
	   1, record->header.length - sizeof(record->header),
	   abfd);
}
static size_t
DEFUN(oasys_string_length,(record),
      oasys_record_union_type *record)
{
return  record->header.length
	- ((char *)record->symbol.name - (char *)record);
}

/*****************************************************************************/

/*

Slurp the symbol table by reading in all the records at the start file
till we get to the first section record.

We'll sort the symbolss into  two lists, defined and undefined. The
undefined symbols will be placed into the table according to their
refno. 

We do this by placing all undefined symbols at the front of the table
moving in, and the defined symbols at the end of the table moving back.

*/

static boolean
DEFUN(oasys_slurp_symbol_table,(abfd),
    bfd * CONST abfd)
{
  oasys_record_union_type record;
  oasys_data_type *data = oasys_data(abfd);
  boolean loop = true;
  asymbol *dest_undefined;
  asymbol *dest_defined;
  asymbol *dest;
  char *string_ptr;


  if (data->symbols != (asymbol *)NULL) {
    return true;
  }
  /* Buy enough memory for all the symbols and all the names */
  data->symbols = 
    (asymbol *)oasys_malloc(abfd, sizeof(asymbol) * abfd->symcount);
#ifdef UNDERSCORE_HACK
  /* buy 1 more char for each symbol to keep the underscore in*/
  data->strings = oasys_malloc(abfd, data->symbol_string_length +
			       abfd->symcount);
#else
  data->strings = oasys_malloc(abfd, data->symbol_string_length);
#endif

  dest_undefined = data->symbols;
  dest_defined = data->symbols + abfd->symcount -1;

  string_ptr = data->strings;
  bfd_seek(abfd, (file_ptr)0, SEEK_SET);
  while (loop) {

    oasys_read_record(abfd, &record);
    switch (record.header.type) {
    case oasys_record_is_header_enum:
      break;
    case oasys_record_is_local_enum:
    case oasys_record_is_symbol_enum:
	{
	  int 	  flag = record.header.type == oasys_record_is_local_enum ?
	    (BSF_LOCAL) : (BSF_GLOBAL | BSF_EXPORT);


	  size_t length = oasys_string_length(&record);
	  switch (record.symbol.relb & RELOCATION_TYPE_BITS) {
	  case RELOCATION_TYPE_ABS:
	    dest = dest_defined--;
	    dest->section = 0;
	    dest->flags = BSF_ABSOLUTE | flag;
	    break;
	  case RELOCATION_TYPE_REL:
	    dest = dest_defined--;
	    dest->section =
	      oasys_data(abfd)->sections[record.symbol.relb &
					 RELOCATION_SECT_BITS];
	    if (record.header.type == oasys_record_is_local_enum) 
		{
		  dest->flags = BSF_LOCAL;
		}
	    else {

	      dest->flags = flag;
	    }
	    break;
	  case RELOCATION_TYPE_UND:
	    dest = data->symbols + bfd_h_getshort(abfd, &record.symbol.refno[0]);
	    dest->section = (asection *)NULL;
	    dest->flags = BSF_UNDEFINED;
	    break;
	  case RELOCATION_TYPE_COM:
	    dest = dest_defined--;
	    dest->name = string_ptr;
	    dest->the_bfd = abfd;

	    dest->section = (asection *)NULL;
	    dest->flags = BSF_FORT_COMM;
	    break;
	  }
	  dest->name = string_ptr;
	  dest->the_bfd = abfd;
	  dest->udata = (PTR)NULL;
	  dest->value = bfd_h_getlong(abfd, &record.symbol.value[0]);

#ifdef UNDERSCORE_HACK
	  if (record.symbol.name[0] != '_') {
	    string_ptr[0] = '_';
	    string_ptr++;
	  }
#endif
	  memcpy(string_ptr, record.symbol.name, length);


	  string_ptr[length] =0;
	  string_ptr += length +1;
	}
      break;
    default:
      loop = false;
    }
  }
  return true;

}

static size_t
DEFUN(oasys_get_symtab_upper_bound,(abfd),
     bfd *CONST abfd)
{
  oasys_slurp_symbol_table (abfd);

  return    (abfd->symcount+1) * (sizeof (oasys_symbol_type *));
}

/* 
*/

extern bfd_target oasys_vec;

unsigned int
DEFUN(oasys_get_symtab,(abfd, location),
      bfd *abfd AND
      asymbol **location)
{
  asymbol *symbase ;
  unsigned int counter ;
  if (oasys_slurp_symbol_table(abfd) == false) {
    return 0;
  }
  symbase = oasys_data(abfd)->symbols;
  for (counter = 0; counter < abfd->symcount; counter++) {
    *(location++) = symbase++;
  }
  *location = 0;
  return abfd->symcount;
}

/***********************************************************************
*  archive stuff 
*/
#define swap(x) x = bfd_h_get_x(abfd, &x);
static bfd_target *
DEFUN(oasys_archive_p,(abfd),
      bfd *abfd)
{
  oasys_archive_header_type header;
  unsigned int i;
  
  bfd_seek(abfd, (file_ptr) 0, false);

  
  bfd_read(&header, 1, sizeof(header), abfd);

  
  swap(header.version);
  swap(header.mod_count);
  swap(header.mod_tbl_offset);
  swap(header.sym_tbl_size);
  swap(header.sym_count);
  swap(header.sym_tbl_offset);
  swap(header.xref_count);
  swap(header.xref_lst_offset);

  /*
     There isn't a magic number in an Oasys archive, so the best we
     can do to verify reasnableness is to make sure that the values in
     the header are too weird
     */

  if (header.version>10000 ||
      header.mod_count>10000 ||
      header.sym_count>100000 ||
      header.xref_count > 100000) return (bfd_target *)NULL;

  /*
     That all worked, lets buy the space for the header and read in
     the headers.
     */
  {
    oasys_ar_data_type *ar =
      (oasys_ar_data_type*) oasys_malloc(abfd, sizeof(oasys_ar_data_type));


    oasys_module_info_type *module = 
      (oasys_module_info_type*)
	oasys_malloc(abfd, sizeof(oasys_module_info_type) * header.mod_count);

    oasys_module_table_type record;

    oasys_ar_data(abfd) =ar;
    ar->module = module;
    ar->module_count = header.mod_count;

    bfd_seek(abfd , header.mod_tbl_offset, SEEK_SET);
    for (i = 0; i < header.mod_count; i++) {

      bfd_read(&record, 1, sizeof(record), abfd);
      swap(record.mod_size);
      swap(record.file_offset);
      swap(record.mod_name_length);
      module[i].name = oasys_malloc(abfd,record.mod_name_length+1);

      bfd_read(module[i].name, 1, record.mod_name_length +1, abfd);
      /* SKip some stuff */
      bfd_seek(abfd, record.dep_count * sizeof(int32_type),
	    SEEK_CUR);

      module[i].size = record.mod_size;
      module[i].pos = record.file_offset;
    }
      
  }
  return abfd->xvec;
}

static boolean
DEFUN(oasys_mkobject,(abfd),
      bfd *abfd)
{
  struct obstack tmp_obstack;
  oasys_data_type *oasys;
  obstack_init(&tmp_obstack);
  BFD_ASSERT(oasys_data(abfd) == 0);
  oasys_data(abfd) =
    (oasys_data_type*)obstack_alloc(&tmp_obstack,sizeof(oasys_data_type));
  oasys = oasys_data(abfd);
  oasys->oasys_obstack = tmp_obstack;
  return true;
}

#define MAX_SECS 16
static bfd_target *
DEFUN(oasys_object_p,(abfd),
      bfd *abfd)
{
  oasys_data_type *oasys;
  boolean loop = true;
  boolean had_usefull = false;
  oasys_data(abfd) = 0;
  oasys_mkobject(abfd);
  oasys = oasys_data(abfd);
  memset((PTR)oasys->sections, 0xff, sizeof(oasys->sections));
    
  /* Point to the start of the file */
  bfd_seek(abfd, (file_ptr)0, SEEK_SET);
  oasys->symbol_string_length = 0;
  /* Inspect the records, but only keep the section info -
     remember the size of the symbols
     */
  oasys->first_data_record = 0;
  while (loop) {
    oasys_record_union_type record;
    oasys_read_record(abfd, &record);
    if (record.header.length < sizeof(record.header))
      goto fail;


    switch ((oasys_record_enum_type)(record.header.type)) {
    case oasys_record_is_header_enum:
      had_usefull = true;
      break;
    case oasys_record_is_symbol_enum:
    case oasys_record_is_local_enum:
      /* Count symbols and remember their size for a future malloc   */
      abfd->symcount++;
      oasys->symbol_string_length += 1 + oasys_string_length(&record);
      had_usefull = true;
      break;
    case oasys_record_is_section_enum:
	{
	  asection *s;
	  char *buffer;
	  unsigned int section_number;
	  if (record.section.header.length != sizeof(record.section))
	      {
		goto fail;
	      }
	  buffer = oasys_malloc(abfd, 3);
	  section_number= record.section.relb & RELOCATION_SECT_BITS;
	  sprintf(buffer,"%u", section_number);
	  s = bfd_make_section(abfd,buffer);
	  oasys->sections[section_number] = s;
	  switch (record.section.relb & RELOCATION_TYPE_BITS) {
	  case RELOCATION_TYPE_ABS:
	  case RELOCATION_TYPE_REL:
	    break;
	  case RELOCATION_TYPE_UND:
	  case RELOCATION_TYPE_COM:
	    BFD_FAIL();
	  }

	  s->size  = bfd_h_getlong(abfd, & record.section.value[0]) ;
	  s->vma = bfd_h_getlong(abfd, &record.section.vma[0]);
	  s->flags |= SEC_LOAD | SEC_HAS_CONTENTS;
	  had_usefull = true;
	}
      break;
    case oasys_record_is_data_enum:
      oasys->first_data_record = bfd_tell(abfd) - record.header.length;
    case oasys_record_is_debug_enum:
    case oasys_record_is_module_enum:
    case oasys_record_is_named_section_enum:
    case oasys_record_is_end_enum:
      if (had_usefull == false) goto fail;
      loop = false;
      break;
    default:
      goto fail;
    }
  }
  oasys->symbols = (asymbol *)NULL;
  /* 
    Oasys support several architectures, but I can't see a simple way
    to discover which one is in a particular file - we'll guess 
    */
  abfd->obj_arch = bfd_arch_m68k;
  abfd->obj_machine =0;
  if (abfd->symcount != 0) {
    abfd->flags |= HAS_SYMS;
  }
  return abfd->xvec;

 fail:
  (void)  obstack_finish(&oasys->oasys_obstack);
  return (bfd_target *)NULL;
}


static void 
DEFUN(oasys_print_symbol,(ignore_abfd, file,  symbol, how),
      bfd *ignore_abfd AND
      FILE *file AND
      asymbol *symbol AND
      bfd_print_symbol_enum_type how)
{
  switch (how) {
  case bfd_print_symbol_name_enum:
  case bfd_print_symbol_type_enum:
    fprintf(file,"%s", symbol->name);
    break;
  case bfd_print_symbol_all_enum:
    {
CONST      char *section_name = symbol->section == (asection *)NULL ?
	"*abs" : symbol->section->name;

      bfd_print_symbol_vandf((PTR)file,symbol);

      fprintf(file," %-5s %s",
	      section_name,
	      symbol->name);
    }
    break;
  }
}
/*
 The howto table is build using the top two bits of a reloc byte to
 index into it. The bits are PCREL,WORD/LONG
*/
static reloc_howto_type howto_table[]= 
{
/* T rs size bsz pcrel bitpos abs ovr sf name partial inplace mask */

{  0, 0,  1,   16, false,0,   true,true,0,"abs16",true,0x0000ffff, 0x0000ffff},
{  0, 0,  2,   32, false,0,   true,true,0,"abs32",true,0xffffffff, 0xffffffff},
{  0, 0,  1,   16, true,0,   true,true,0,"pcrel16",true,0x0000ffff, 0x0000ffff},
{  0, 0,  2,   32, true,0,   true,true,0,"pcrel32",true,0xffffffff, 0xffffffff}
};

/* Read in all the section data and relocation stuff too */
static boolean 
DEFUN(oasys_slurp_section_data,(abfd),
  bfd *CONST abfd)
{
  oasys_record_union_type record;
  oasys_data_type *data = oasys_data(abfd);
  boolean loop = true;

  oasys_per_section_type *per ;

  asection *s;

  /* Buy enough memory for all the section data and relocations */
  for (s = abfd->sections; s != (asection *)NULL; s= s->next) {
    per =  oasys_per_section(s);
    if (per->data != (bfd_byte*)NULL) return true;
    per->data = (bfd_byte *) oasys_malloc(abfd, s->size);
    per->reloc_tail_ptr = (oasys_reloc_type **)&(s->relocation);
    per->had_vma = false;
    s->reloc_count = 0;
  }

  if (data->first_data_record == 0)  return true;
  bfd_seek(abfd, data->first_data_record, SEEK_SET);
  while (loop) {
    oasys_read_record(abfd, &record);
    switch (record.header.type) {
    case oasys_record_is_header_enum:
      break;
    case oasys_record_is_data_enum:
	{

	  uint8e_type *src = record.data.data;
	  uint8e_type *end_src = ((uint8e_type *)&record) +
	    record.header.length;
	  unsigned int relbit;
	  bfd_byte *dst_ptr ;
	  bfd_byte *dst_base_ptr ;
	  unsigned int count;
	  asection *  section =
	    data->sections[record.data.relb & RELOCATION_SECT_BITS];
	  bfd_vma dst_offset ;
	  per =  oasys_per_section(section);
	  dst_offset = bfd_h_getlong(abfd, record.data.addr) ;
	  if (per->had_vma == false) {
	    /* Take the first vma we see as the base */

	    section->vma = dst_offset;
	    per->had_vma = true;
	  }


	  dst_offset -=   section->vma;


	  dst_base_ptr = oasys_per_section(section)->data;
	  dst_ptr = oasys_per_section(section)->data +
	    dst_offset;

	  while (src < end_src) {
	    uint32_type gap = end_src - src -1;
	    uint8e_type mod_byte = *src++;
	    count = 8;
	    if (mod_byte == 0 && gap >= 8) {
	      dst_ptr[0] = src[0];
	      dst_ptr[1] = src[1];
	      dst_ptr[2] = src[2];
	      dst_ptr[3] = src[3];
	      dst_ptr[4] = src[4];
	      dst_ptr[5] = src[5];
	      dst_ptr[6] = src[6];
	      dst_ptr[7] = src[7];
	      dst_ptr+= 8;
	      src += 8;
	    }
	    else {
	      for (relbit = 1; count-- != 0 && gap != 0; gap --, relbit <<=1) 
		  {
		    if (relbit & mod_byte) 
			{
			  uint8e_type reloc = *src;
			  /* This item needs to be relocated */
			  switch (reloc & RELOCATION_TYPE_BITS) {
			  case RELOCATION_TYPE_ABS:

			    break;

			  case RELOCATION_TYPE_REL: 
			      {
				/* Relocate the item relative to the section */
				oasys_reloc_type *r =
				  (oasys_reloc_type *)
				    oasys_malloc(abfd,
						 sizeof(oasys_reloc_type));
				*(per->reloc_tail_ptr) = r;
				per->reloc_tail_ptr = &r->next;
				r->next= (oasys_reloc_type *)NULL;
				/* Reference to undefined symbol */
				src++;
				/* There is no symbol */
				r->symbol = 0;
				/* Work out the howto */
				r->relent.section =
				  data->sections[reloc & RELOCATION_SECT_BITS];
				r->relent.addend = - r->relent.section->vma;
				r->relent.address = dst_ptr - dst_base_ptr;
				r->relent.howto = &howto_table[reloc>>6];
				r->relent.sym_ptr_ptr = (asymbol **)NULL;
				section->reloc_count++;
			      }
			    break;


			  case RELOCATION_TYPE_UND:
			      { 
				oasys_reloc_type *r =
				  (oasys_reloc_type *)
				    oasys_malloc(abfd,
						 sizeof(oasys_reloc_type));
				*(per->reloc_tail_ptr) = r;
				per->reloc_tail_ptr = &r->next;
				r->next= (oasys_reloc_type *)NULL;
				/* Reference to undefined symbol */
				src++;
				/* Get symbol number */
				r->symbol = (src[0]<<8) | src[1];
				/* Work out the howto */
				r->relent.section = (asection *)NULL;
				r->relent.addend = 0;
				r->relent.address = dst_ptr - dst_base_ptr;
				r->relent.howto = &howto_table[reloc>>6];
				r->relent.sym_ptr_ptr = (asymbol **)NULL;
				section->reloc_count++;

				src+=2;
			      }
			    break;
			  case RELOCATION_TYPE_COM:
			    BFD_FAIL();
			  }
			}
		    *dst_ptr++ = *src++;
		  }
	    }
	  }	  
	}
      break;
    case oasys_record_is_local_enum:
    case oasys_record_is_symbol_enum:
    case oasys_record_is_section_enum:
      break;
    default:
      loop = false;
    }
  }
  return true;

}



bfd_error_vector_type bfd_error_vector;

static boolean
DEFUN(oasys_new_section_hook,(abfd, newsect),
      bfd *abfd AND
      asection *newsect)
{
  newsect->used_by_bfd = (oasys_per_section_type *)
    oasys_malloc(abfd, sizeof(oasys_per_section_type));
  oasys_per_section( newsect)->data = (bfd_byte *)NULL;
  oasys_per_section(newsect)->section = newsect;
  oasys_per_section(newsect)->offset  = 0;
  newsect->alignment_power = 3;
  /* Turn the section string into an index */

  sscanf(newsect->name,"%u", &newsect->target_index);

  return true;
}


static unsigned int
DEFUN(oasys_get_reloc_upper_bound, (abfd, asect),
      bfd *abfd AND
      sec_ptr asect)
{
  oasys_slurp_section_data(abfd);
  return (asect->reloc_count+1) * sizeof(arelent *);
}

static boolean
DEFUN(oasys_get_section_contents,(abfd, section, location, offset, count),
      bfd *abfd AND
      sec_ptr section AND
      void  *location AND
      file_ptr offset AND
      unsigned int count)
{
  oasys_per_section_type *p = section->used_by_bfd;
  oasys_slurp_section_data(abfd);
  (void)  memcpy(location, p->data + offset, count);
  return true;
}


unsigned int
DEFUN(oasys_canonicalize_reloc,(abfd, section, relptr, symbols),
      bfd *abfd AND
      sec_ptr section AND
      arelent **relptr AND
      asymbol **symbols)
{
  unsigned int reloc_count = 0;
  oasys_reloc_type *src = (oasys_reloc_type *)(section->relocation);
  while (src != (oasys_reloc_type *)NULL) {
    if (src->relent.section == (asection *)NULL) 
	{
	  src->relent.sym_ptr_ptr = symbols + src->symbol;
	}
    *relptr ++ = &src->relent;
    src = src->next;
    reloc_count++;
  }
  *relptr = (arelent *)NULL;
  return section->reloc_count = reloc_count;
}


boolean
DEFUN(oasys_set_arch_mach, (abfd, arch, machine),
      bfd *abfd AND
      enum bfd_architecture arch AND
      unsigned long machine)
{
  abfd->obj_arch = arch;
  abfd->obj_machine = machine;
  return true;
}



/* Writing */


/* Calculate the checksum and write one record */
static void 
DEFUN(oasys_write_record,(abfd, type, record, size),
      bfd *CONST abfd AND
      CONST oasys_record_enum_type type AND
      oasys_record_union_type *record AND
      CONST size_t size)
{
  int checksum;
  size_t i;
  uint8e_type *ptr;
  record->header.length = size;
  record->header.type = type;
  record->header.check_sum = 0;
  record->header.fill = 0;
  ptr = &record->pad[0];
  checksum = 0;
  for (i = 0; i < size; i++) {
    checksum += *ptr++;
  }
  record->header.check_sum = 0xff & (- checksum);
  bfd_write((PTR)record, 1, size, abfd);
}


/* Write out all the symbols */
static void 
DEFUN(oasys_write_syms, (abfd),
      bfd * CONST abfd)
{
  unsigned int count;
  asymbol **generic = bfd_get_outsymbols(abfd);
  unsigned int index = 0;
  for (count = 0; count < bfd_get_symcount(abfd); count++) {

    oasys_symbol_record_type symbol;
    asymbol * CONST g = generic[count];

    CONST    char *src = g->name;
    char *dst = symbol.name;
    unsigned int l = 0;

    if (g->flags & BSF_FORT_COMM) {
      symbol.relb = RELOCATION_TYPE_COM;
      bfd_h_putshort(abfd, index, (uint8e_type *)(&symbol.refno[0]));
      index++;
    }
    else if (g->flags & BSF_ABSOLUTE) {
      symbol.relb = RELOCATION_TYPE_ABS;
      bfd_h_putshort(abfd, 0, (uint8e_type *)(&symbol.refno[0]));

    }
    else if (g->flags & BSF_UNDEFINED) {
      symbol.relb = RELOCATION_TYPE_UND ;
      bfd_h_putshort(abfd, index, (uint8e_type *)(&symbol.refno[0]));
      /* Overload the value field with the output index number */
      index++;
    }
    else if (g->flags & BSF_DEBUGGING) {
      /* throw it away */
      continue;
    }
    else {
      symbol.relb = RELOCATION_TYPE_REL | g->section->output_section->target_index;
      bfd_h_putshort(abfd, 0, (uint8e_type *)(&symbol.refno[0]));
    }
    while (src[l]) {
      dst[l] = src[l];
      l++;
    }

    bfd_h_putlong(abfd, g->value, symbol.value);

      
    if (g->flags & BSF_LOCAL) {
      oasys_write_record(abfd, 	
			 oasys_record_is_local_enum,
			 (oasys_record_union_type *) &symbol,
			 offsetof(oasys_symbol_record_type, name[0]) + l);
    }
    else {
      oasys_write_record(abfd, 	
			 oasys_record_is_symbol_enum,
			 (oasys_record_union_type *) &symbol,
			 offsetof(oasys_symbol_record_type, name[0]) + l);
    }
    g->value = index-1;
  }
}


  /* Write a section header for each section */
static void 
DEFUN(oasys_write_sections, (abfd),
      bfd *CONST abfd)
{
  asection *s;
  static  oasys_section_record_type out = {0};

  for (s = abfd->sections; s != (asection *)NULL; s = s->next) {
    if (!isdigit(s->name[0])) 
	{
          bfd_error_vector.nonrepresentable_section(abfd,
						    s->name);
	}
    out.relb = RELOCATION_TYPE_REL | s->target_index;
    bfd_h_putlong(abfd, s->size, out.value);
    bfd_h_putlong(abfd, s->vma, out.vma);

    oasys_write_record(abfd,
		       oasys_record_is_section_enum,
		       (oasys_record_union_type *) &out,
		       sizeof(out));
  }
}

static void
DEFUN(oasys_write_header, (abfd),
      bfd *CONST abfd)
{
  /* Create and write the header */
  oasys_header_record_type r;
  size_t length = strlen(abfd->filename);
  if (length > sizeof(r.module_name)) {
    length = sizeof(r.module_name);
  }

  (void)memcpy(r.module_name,
	       abfd->filename,
	       length);
  (void)memset(r.module_name + length,
	       ' ',
	       sizeof(r.module_name) - length);

  r.version_number = OASYS_VERSION_NUMBER;
  r.rev_number = OASYS_REV_NUMBER;
  oasys_write_record(abfd,
		     oasys_record_is_header_enum,
		     (oasys_record_union_type *)&r,
		     offsetof(oasys_header_record_type, description[0]));



}

static void
DEFUN(oasys_write_end,(abfd),
      bfd *CONST abfd)
{
  oasys_end_record_type end;
  end.relb = RELOCATION_TYPE_ABS;
  bfd_h_putlong(abfd, abfd->start_address, end.entry); 
  bfd_h_putshort(abfd, 0, end.fill);
  end.zero =0;
  oasys_write_record(abfd,
		     oasys_record_is_end_enum,
		     (oasys_record_union_type *)&end,
		     sizeof(end));
}

static int 
DEFUN(comp,(ap, bp),
      arelent **ap AND
      arelent **bp)
{
  arelent *a = *ap;
  arelent *b = *bp;
  return a->address - b->address;
}

/*
 Writing data..
 
*/
static void
DEFUN(oasys_write_data, (abfd),
      bfd *CONST abfd)
{
  asection *s;
  for (s = abfd->sections; s != (asection *)NULL; s = s->next) {
    uint8e_type *raw_data = oasys_per_section(s)->data;
    oasys_data_record_type processed_data;
    unsigned int current_byte_index = 0;
    unsigned int relocs_to_go = s->reloc_count;
    arelent **p = s->orelocation;
    if (s->reloc_count != 0) {
      /* Sort the reloc records so it's easy to insert the relocs into the
	 data */
    
      qsort(s->orelocation,
	    s->reloc_count,
	    sizeof(arelent **),
	    comp);
    }
    current_byte_index = 0;
    processed_data.relb = s->target_index | RELOCATION_TYPE_REL;

    while (current_byte_index < s->size) 
	{
	  /* Scan forwards by eight bytes or however much is left and see if
	     there are any relocations going on */
	  uint8e_type *mod = &processed_data.data[0];
	  uint8e_type *dst = &processed_data.data[1];

	  unsigned int i;
	  unsigned int long_length = 128;


	  bfd_h_putlong(abfd, s->vma + current_byte_index, processed_data.addr);
	  if (long_length + current_byte_index > s->size) {
	    long_length = s->size - current_byte_index;
	  }
	  while (long_length  > 0 &&  (dst - (uint8e_type*)&processed_data < 128)) {
	    
	    unsigned int length = long_length;
	    *mod =0;
	    if (length > 8)
	      length = 8;

	    for (i = 0; i < length; i++) {
	      if (relocs_to_go != 0) {	
		arelent *r = *p;
		reloc_howto_type *CONST how=r->howto;
		/* There is a relocation, is it for this byte ? */
		if (r->address == current_byte_index) {
		  uint8e_type rel_byte;
		  p++;
		  relocs_to_go--;

		  *mod |= (1<<i);
		  if(how->pc_relative) {
		    rel_byte = 0x80;
		  }
		  else {
		    rel_byte = 0;
		  }
		  if (how->size ==2) {
		    rel_byte |= 0x40;
		  }

		  /* Is this a section relative relocation, or a symbol
		     relative relocation ? */
		  if (r->section != (asection*)NULL) 
		      {
			/* The relent has a section attatched, so it must be section
			   relative */
			rel_byte |= RELOCATION_TYPE_REL;
			rel_byte |= r->section->output_section->target_index;
			*dst++ = rel_byte;
		      }
		  else 
		      {
			asymbol *p = *(r->sym_ptr_ptr);

			/* If this symbol has a section attatched, then it
			   has already been resolved.  Change from a symbol
			   ref to a section ref */
			if(p->section != (asection *)NULL) {
			  rel_byte |= RELOCATION_TYPE_REL;
			  rel_byte |=
			    p->section->output_section->target_index;
			  *dst++ = rel_byte;
			}
			else {
			  rel_byte |= RELOCATION_TYPE_UND;
		  

			  *dst++ = rel_byte;
			  /* Next two bytes are a symbol index - we can get
			     this from the symbol value which has been zapped
			     into the symbol index in the table when the
			     symbol table was written
			     */
			  *dst++ = p->value >> 8;
			  *dst++ = p->value;
			}

		      }
		}
	      }
	      /* If this is coming from an unloadable section then copy
		 zeros */
	      if (raw_data == (uint8e_type *)NULL) {
		*dst++ = 0;
	      }
	      else {
		*dst++ = *raw_data++;
	      }
	      current_byte_index++;
	    }
	    mod = dst++;
	    long_length -= length;
	  }

	  oasys_write_record(abfd,
			     oasys_record_is_data_enum,
			     (oasys_record_union_type *)&processed_data,
			     dst - (uint8e_type*)&processed_data);
			 
	}
  }
}
static boolean
DEFUN(oasys_write_object_contents, (abfd),
      bfd * CONST abfd)
{
  oasys_write_header(abfd);
  oasys_write_syms(abfd);
  oasys_write_sections(abfd);
  oasys_write_data(abfd);
  oasys_write_end(abfd);
  return true;
}




/** exec and core file sections */

/* set section contents is complicated with OASYS since the format is 
* not a byte image, but a record stream.
*/
static boolean
DEFUN(oasys_set_section_contents,(abfd, section, location, offset, count),
      bfd *abfd AND
      sec_ptr section AND 
      unsigned char *location AND
      file_ptr offset AND
      int count)
{
  if (count != 0) {
    if (oasys_per_section(section)->data == (bfd_byte *)NULL ) 
	{
	  oasys_per_section(section)->data =
	    (bfd_byte *)(oasys_malloc(abfd,section->size));    
	}
    (void) memcpy(oasys_per_section(section)->data + offset,
		  location,
		  count);
  }
  return true;
}



/* Native-level interface to symbols. */

/* We read the symbols into a buffer, which is discarded when this
function exits.  We read the strings into a buffer large enough to
hold them all plus all the cached symbol entries. */

static asymbol *
DEFUN(oasys_make_empty_symbol,(abfd),
      bfd *abfd)
{

  oasys_symbol_type  *new =
    (oasys_symbol_type *)zalloc (sizeof (oasys_symbol_type));
  new->symbol.the_bfd = abfd;
  return &new->symbol;

}



/* Obsbolete procedural interface; better to look at the cache directly */

/* User should have checked the file flags; perhaps we should return
BFD_NO_MORE_SYMBOLS if there are none? */



boolean
oasys_close_and_cleanup (abfd)
bfd *abfd;
{
  if (bfd_read_p (abfd) == false)
    switch (abfd->format) {
    case bfd_archive:
      if (!_bfd_write_archive_contents (abfd)) {
	return false;
      }
      break;
    case bfd_object:
      if (!oasys_write_object_contents (abfd)) {
	return false;
      }
      break;
    default:
      bfd_error = invalid_operation;
      return false;
    }


  if (oasys_data(abfd) != (oasys_data_type *)NULL) {
    /* It's so easy to throw everything away */
(void)    obstack_finish(&(oasys_data(abfd)->oasys_obstack));
  }

  return true;
}

static bfd *
oasys_openr_next_archived_file(arch, prev)
bfd *arch;
bfd *prev;
{
  oasys_ar_data_type *ar = oasys_ar_data(arch);
  oasys_module_info_type *p;
  /* take the next one from the arch state, or reset */
  if (prev == (bfd *)NULL) {
    /* Reset the index - the first two entries are bogus*/
    ar->module_index = 0;
  }

  p = ar->module + ar->module_index;
  ar->module_index++;

  if (ar->module_index <= ar->module_count) {
    if (p->abfd == (bfd *)NULL) {
      p->abfd = _bfd_create_empty_archive_element_shell(arch);
      p->abfd->origin = p->pos;
      p->abfd->filename = p->name;

      /* Fixup a pointer to this element for the member */
      p->abfd->arelt_data = (PTR)p;
    }
    return p->abfd;
  }
  else {
    bfd_error = no_more_archived_files;
    return (bfd *)NULL;
  }
}

static boolean
oasys_find_nearest_line(abfd,
			 section,
			 symbols,
			 offset,
			 filename_ptr,
			 functionname_ptr,
			 line_ptr)
bfd *abfd;
asection *section;
asymbol **symbols;
bfd_vma offset;
char **filename_ptr;
char **functionname_ptr;
unsigned int *line_ptr;
{
  return false;

}

static int
oasys_stat_arch_elt(abfd, buf)
bfd *abfd;
struct stat *buf;
{
  oasys_module_info_type *mod = abfd->arelt_data;
  if (mod == (oasys_module_info_type *)NULL) {
    bfd_error = invalid_operation;
    return -1;
  }
  else {
    buf->st_size = mod->size;
    buf->st_mode = 0666;
  return 0;
  }


}


/*SUPPRESS 460 */
bfd_target oasys_vec =
{
  "oasys",			/* name */
  bfd_target_oasys_flavour_enum,
  true,				/* target byte order */
  true,				/* target headers byte order */
  (HAS_RELOC | EXEC_P |		/* object flags */
   HAS_LINENO | HAS_DEBUG |
   HAS_SYMS | HAS_LOCALS | DYNAMIC | WP_TEXT | D_PAGED),
  (SEC_CODE|SEC_DATA|SEC_ROM|SEC_HAS_CONTENTS
   |SEC_ALLOC | SEC_LOAD | SEC_RELOC), /* section flags */
  0,				/* valid reloc types */
  ' ',				/* ar_pad_char */
  16,				/* ar_max_namelen */
  oasys_close_and_cleanup,	/* _close_and_cleanup */
  oasys_set_section_contents,	/* bfd_set_section_contents */
  oasys_get_section_contents,
  oasys_new_section_hook,	/*   new_section_hook */
  0,				/* _core_file_failing_command */
  0,				/* _core_file_failing_signal */
  0,				/* _core_file_matches_ex...p */

  0,				/* bfd_slurp_bsd_armap,		      bfd_slurp_armap */
  bfd_true,			/* bfd_slurp_extended_name_table */
  bfd_bsd_truncate_arname,	/* bfd_truncate_arname */

  oasys_get_symtab_upper_bound,	/* get_symtab_upper_bound */
  oasys_get_symtab,		/* canonicalize_symtab */
  0,				/* oasys_reclaim_symbol_table,            bfd_reclaim_symbol_table */
  oasys_get_reloc_upper_bound,	/* get_reloc_upper_bound */
  oasys_canonicalize_reloc,	/* bfd_canonicalize_reloc */
  0,				/*  oasys_reclaim_reloc,                   bfd_reclaim_reloc */
  0,				/* oasys_get_symcount_upper_bound,        bfd_get_symcount_upper_bound */
  0,				/* oasys_get_first_symbol,                bfd_get_first_symbol */
  0,				/* oasys_get_next_symbol,                 bfd_get_next_symbol */
  0,				/* oasys_classify_symbol,                 bfd_classify_symbol */
  0,				/* oasys_symbol_hasclass,                 bfd_symbol_hasclass */
  0,				/* oasys_symbol_name,                     bfd_symbol_name */
  0,				/* oasys_symbol_value,                    bfd_symbol_value */

  _do_getblong, _do_putblong, _do_getbshort, _do_putbshort, /* data */
  _do_getblong, _do_putblong, _do_getbshort, _do_putbshort, /* hdrs */

  {_bfd_dummy_target,
     oasys_object_p,		/* bfd_check_format */
     oasys_archive_p,
     bfd_false
     },
  {
    bfd_false,
    oasys_mkobject, 
    _bfd_generic_mkarchive,
    bfd_false
    },
  oasys_make_empty_symbol,
  oasys_print_symbol,
  bfd_false,			/*	oasys_get_lineno,*/
  oasys_set_arch_mach,		/* bfd_set_arch_mach,*/
  bfd_false,
  oasys_openr_next_archived_file,
  oasys_find_nearest_line,	/* bfd_find_nearest_line */
  oasys_stat_arch_elt,		/* bfd_stat_arch_elt */
};
