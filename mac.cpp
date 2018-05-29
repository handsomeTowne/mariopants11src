// mac.cpp
//   mac specific implementation of os.h

#include <cstdlib>
#include <cstring>
#include <cstdio>
#include "os.h"

// defined in mac_cocoa.m
extern "C" {
extern void cocoa_alert(const char* message);
extern int cocoa_yesno(const char* message);
extern const char* cocoa_file_load(const char* default_name, int mask_count, const char** masks);
extern const char* cocoa_file_save(const char* default_name, int mask_count, const char** masks);
}

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
	return cocoa_alert(message);
}

bool yesno(const char* message)
{
	return cocoa_yesno(message) == 1;
}

const char* file_load(const char* default_name, int mask_count, const char** masks)
{
	return cocoa_file_load(default_name,mask_count,masks);
}

const char* file_save(const char* default_name, int mask_count, const char** masks)
{
	return cocoa_file_save(default_name,mask_count,masks);
}

} // namespace os

// end of file
