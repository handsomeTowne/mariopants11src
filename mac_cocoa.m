// mac_cocoa.m
//   objective C bridge for cocoa gui to be used by mac.cpp

#import <Cocoa/Cocoa.h>
#include <string.h>

extern void cocoa_alert(const char* message);
extern int cocoa_yesno(const char* message);
extern const char* cocoa_file_load(const char* default_name, int mask_count, const char** masks);
extern const char* cocoa_file_save(const char* default_name, int mask_count, const char** masks);
	
void cocoa_alert(const char* message)
{
	NSString* ns_message = [NSString stringWithCString:message encoding:NSASCIIStringEncoding];
	NSAlert* alert = [[NSAlert alloc] init];
	[alert addButtonWithTitle:@"OK"];
	[alert setMessageText:@"Alert!"];
	[alert setInformativeText:ns_message];
	[alert setAlertStyle:NSWarningAlertStyle];
	[alert runModal];
	[alert release];
}

int cocoa_yesno(const char* message)
{
	NSString* ns_message = [NSString stringWithCString:message encoding:NSASCIIStringEncoding];
	NSAlert* alert = [[NSAlert alloc] init];
	[alert addButtonWithTitle:@"OK"];
	[alert addButtonWithTitle:@"Cancel"];
	[alert setMessageText:@"Question!"];
	[alert setInformativeText:ns_message];
	[alert setAlertStyle:NSInformationalAlertStyle];
	
	int result = ([alert runModal] == NSAlertFirstButtonReturn) ? 1 : 0;
	[alert release];
	
	return result;
}

const char* cocoa_file_load(const char* default_name, int mask_count, const char** masks)
{
	static char file[1024];
	strcpy(file, default_name);
	
	NSMutableArray* ma = [[NSMutableArray alloc] init];
	for (int i=0; i<mask_count; ++i)
	{
		if (strlen(masks[i]) > 2 && masks[i][0] == '*' && masks[i][1] == '.' && strcmp(masks[i],"*.*"))
		{
			const char* mask = masks[i] + 2; // strip *.
			NSString* ns_m = [NSString stringWithCString:mask encoding:NSASCIIStringEncoding];
			[ma addObject:ns_m];
		}
	}
	
	NSOpenPanel* panel = [NSOpenPanel openPanel];
	
	[panel setCanChooseFiles:YES];
	[panel setCanChooseDirectories:NO];
	[panel setAllowsMultipleSelection:NO];
	[panel setAllowedFileTypes:ma];
	
	int i = [panel runModal];
	if (i == NSOKButton)
	{
		NSURL *url = [panel URL];
		strcpy(file,[[url path] UTF8String]);
	}	
	
	[ma release];
	
	if (i == NSOKButton) return file;
	return NULL;
}

const char* cocoa_file_save(const char* default_name, int mask_count, const char** masks)
{
	static char file[1024];
	strcpy(file, default_name);
	
	NSMutableArray* ma = [[NSMutableArray alloc] init];
	for (int i=0; i<mask_count; ++i)
	{
		if (strlen(masks[i]) > 2 && masks[i][0] == '*' && masks[i][1] == '.' && strcmp(masks[i],"*.*"))
		{
			const char* mask = masks[i] + 2; // strip *.
			NSString* ns_m = [NSString stringWithCString:mask encoding:NSASCIIStringEncoding];
			[ma addObject:ns_m];
		}
	}
	
	NSSavePanel* panel = [NSSavePanel savePanel];
	NSString* nsfile = [NSString stringWithUTF8String:file];
	NSURL* default_dir = [NSURL URLWithString:[nsfile stringByDeletingLastPathComponent]];
	
	[panel setDirectoryURL:default_dir];
	[panel setNameFieldStringValue:[nsfile lastPathComponent]];
	[panel setAllowedFileTypes:ma];
	
	int i = [panel runModal];
	if (i == NSOKButton)
	{
		NSURL *url = [panel URL];
		strcpy(file,[[url path] UTF8String]);
	}	
	
	[ma release];
	
	if (i == NSOKButton) return file;
	return NULL;
}
