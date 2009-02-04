#include "vdl-log.h"
#include "avprintf-cb.h"

static void avprintf_callback (char c, void *context)
{
  if (c != 0)
    {
      system_write (2, &c, 1);
    }
}

void vdl_log_set (const char *debug_str)
{
  VDL_LOG_FUNCTION ("debug=%s", debug_str);
  if (debug_str == 0)
    {
      return;
    }
  struct VdlStringList *list = vdl_strsplit (debug_str, ':');
  struct VdlStringList *cur;
  uint32_t logging = 0;
  for (cur = list; cur != 0; cur = cur->next)
    {
      if (vdl_utils_strisequal (cur->str, "debug"))
	{
	  logging |= VDL_LOG_DBG;
	}
      else if (vdl_utils_strisequal (cur->str, "function"))
	{
	  logging |= VDL_LOG_FUNC;
	}
      else if (vdl_utils_strisequal (cur->str, "error"))
	{
	  logging |= VDL_LOG_ERR;
	}
      else if (vdl_utils_strisequal (cur->str, "assert"))
	{
	  logging |= VDL_LOG_AST;
	}
      else if (vdl_utils_strisequal (cur->str, "symbol-fail"))
	{
	  logging |= VDL_LOG_SYM_FAIL;
	}
      else if (vdl_utils_strisequal (cur->str, "symbol-ok"))
	{
	  logging |= VDL_LOG_SYM_OK;
	}
      else if (vdl_utils_strisequal (cur->str, "reloc"))
	{
	  logging |= VDL_LOG_REL;
	}
      else if (vdl_utils_strisequal (cur->str, "help"))
	{
	  VDL_LOG_ERROR ("Available logging levels: debug, function, error, assert, symbol-fail, symbol-ok, reloc\n", 1);
	}
    }
  g_vdl.logging |= logging;
  vdl_utils_str_list_free (list);
}



void vdl_utils_log_printf (enum VdlLog log, const char *str, ...)
{
  va_list list;
  va_start (list, str);
  if (g_vdl.logging & log)
    {
      avprintf_cb (avprintf_callback, 0, str, list);
    }
  va_end (list);
}
