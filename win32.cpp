// win32.cpp
//   win32 specific implementation of os.h

#include <windows.h>
#include "os.h"

static const char* win32_masks(int mask_count, const char** masks)
{
	static char ofm[1024];

	strcpy(ofm, masks[0]);
	char* ofm_end = ofm + strlen(ofm) + 1;
	strcpy(ofm_end, masks[0]);
	ofm_end += strlen(ofm_end) + 1;

	for (int i=1; i < mask_count; ++i)
	{
		strcpy(ofm_end, masks[i]);
		ofm_end += strlen(ofm_end) + 1;
		strcpy(ofm_end, masks[i]);
		ofm_end += strlen(ofm_end) + 1;
	}
	ofm_end[0] = 0;

	return ofm;
}

namespace os
{

void alert(const char* message)
{
	MessageBox(NULL, message, "Alert!", MB_ICONEXCLAMATION | MB_OK);
}

bool yesno(const char* message)
{
	return IDOK == MessageBox(NULL, message, "Question!", MB_ICONQUESTION | MB_OKCANCEL);
}

const char* file_load(const char* default_name, int mask_count, const char** masks)
{
	static char file[1024];
	strcpy(file, default_name);

	static DWORD last_filter = 1;
	if (last_filter > (DWORD)mask_count) last_filter = 1;

	OPENFILENAME of;
	memset(&of, 0, sizeof(of));
	of.lStructSize = sizeof(OPENFILENAME);
	of.lpstrFilter = win32_masks(mask_count,masks);
	of.lpstrFile = file;
	of.nMaxFile = sizeof(file);
	of.Flags = OFN_FILEMUSTEXIST;
	of.nFilterIndex = last_filter;

	if (0 == GetOpenFileName(&of)) return NULL;
	last_filter = of.nFilterIndex; // remember last mask used

	return file;
}

const char* file_save(const char* default_name, int mask_count, const char** masks)
{
	static char file[1024];
	strcpy(file, default_name);

	static DWORD last_filter = 1;

	// knock off extension and select filter if it matches an existing mask
	{
		char* ext = strrchr(file, '.');
		if (ext != NULL)
		{
			for (int i=0; i < mask_count; ++i)
			{
				if (!stricmp(ext,masks[i]+1))
				{
					*ext = 0;
					last_filter = i+1;
					break;
				}
			}
		}
	}

	if (last_filter > (DWORD)mask_count) last_filter = 1;
	OPENFILENAME of;
	memset(&of, 0, sizeof(of));
	of.lStructSize = sizeof(OPENFILENAME);
	of.lpstrFilter = win32_masks(mask_count,masks);
	of.lpstrFile = file;
	of.nMaxFile = sizeof(file);
	of.nFilterIndex = last_filter;

	if (0 == GetSaveFileName(&of)) return NULL;
	last_filter = of.nFilterIndex; // remember last mask used

	// add extension if one does not exist
	{
		const char* ext = strrchr(file, '.');
		if (ext == NULL && last_filter > 0)
		{
			ext = masks[last_filter-1] + 1; // strip "*" from beginning
			if (strcmp(ext,".*") && strlen(file) < (sizeof(file)-(strlen(ext)+2)))
			{
				strcat(file,ext);
			}
		}
	}

	if (INVALID_FILE_ATTRIBUTES != GetFileAttributes(file))
	{
		if (!os::yesno("Overwrite file?"))
		{
			return NULL;
		}
	}

	return file;
}

} // namespace os

// end of file
