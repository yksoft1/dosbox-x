/* Mac OS X portion of menu.cpp */

#include "config.h"
#include "menu.h"

#if DOSBOXMENU_TYPE == DOSBOXMENU_NSMENU /* Mac OS X NSMenu / NSMenuItem handle */
# include <MacTypes.h>
# include <Cocoa/Cocoa.h>
# include <Foundation/NSString.h>

@interface NSApplication (DOSBoxX)
@end

extern "C" void* sdl1_hax_stock_osx_menu(void);

void *sdl_hax_nsMenuItemFromTag(void *nsMenu, unsigned int tag) {
	NSMenuItem *ns_item = [((NSMenu*)nsMenu) itemWithTag: tag];
	return (ns_item != nil) ? ns_item : NULL;
}

void sdl_hax_nsMenuItemUpdateFromItem(void *nsMenuItem, DOSBoxMenu::item &item) {
	if (item.has_changed()) {
		NSMenuItem *ns_item = (NSMenuItem*)nsMenuItem;

		[ns_item setEnabled:(item.is_enabled() ? YES : NO)];
		[ns_item setState:(item.is_checked() ? NSOnState : NSOffState)];

		const std::string &it = item.get_text();
		const std::string &st = item.get_shortcut_text();
		std::string ft = it;

		/* TODO: Figure out how to put the shortcut text right-aligned while leaving the main text left-aligned */
		if (!st.empty()) {
			ft += " [";
			ft += st;
			ft += "]";
		}

		{
			NSString *title = [[NSString alloc] initWithUTF8String:ft.c_str()];
			[ns_item setTitle:title];
			[title release];
		}

		item.clear_changed();
	}
}

void* sdl_hax_nsMenuAlloc(const char *initWithText) {
	NSString *title = [[NSString alloc] initWithUTF8String:initWithText];
	NSMenu *menu = [[NSMenu alloc] initWithTitle: title];
	[title release];
	[menu setAutoenablesItems:NO];
	return (void*)menu;
}

void sdl_hax_nsMenuRelease(void *nsMenu) {
	[((NSMenu*)nsMenu) release];
}

void sdl_hax_macosx_setmenu(void *nsMenu) {
	if (nsMenu != NULL) {
        /* switch to the menu object given */
		[NSApp setMainMenu:((NSMenu*)nsMenu)];
	}
	else {
		/* switch back to the menu SDL 1.x made */
		[NSApp setMainMenu:((NSMenu*)sdl1_hax_stock_osx_menu())];
	}
}

void sdl_hax_nsMenuItemSetTag(void *nsMenuItem, unsigned int new_id) {
	[((NSMenuItem*)nsMenuItem) setTag:new_id];
}

void sdl_hax_nsMenuItemSetSubmenu(void *nsMenuItem,void *nsMenu) {
	[((NSMenuItem*)nsMenuItem) setSubmenu:((NSMenu*)nsMenu)];
}

void* sdl_hax_nsMenuItemAlloc(const char *initWithText) {
	NSString *title = [[NSString alloc] initWithUTF8String:initWithText];
	NSMenuItem *item = [[NSMenuItem alloc] initWithTitle: title action:@selector(DOSBoxXMenuAction:) keyEquivalent:@""];
	[title release];
	return (void*)item;
}

void sdl_hax_nsMenuAddItem(void *nsMenu,void *nsMenuItem) {
	[((NSMenu*)nsMenu) addItem:((NSMenuItem*)nsMenuItem)];
}

void* sdl_hax_nsMenuAllocSeparator(void) {
	return (void*)([NSMenuItem separatorItem]);
}

void sdl_hax_nsMenuItemRelease(void *nsMenuItem) {
	[((NSMenuItem*)nsMenuItem) release];
}

void sdl_hax_nsMenuAddApplicationMenu(void *nsMenu) {
	/* make up an Application menu and stick it in first.
	   the caller should have passed us an empty menu */
	NSMenu *appMenu;
	NSMenuItem *appMenuItem;

	appMenu = [[NSMenu alloc] initWithTitle:@""];
	[appMenu addItemWithTitle:@"About DOSBox-X" action:@selector(orderFrontStandardAboutPanel:) keyEquivalent:@""];

	appMenuItem = [[NSMenuItem alloc] initWithTitle:@"" action:nil keyEquivalent:@""];
	[appMenuItem setSubmenu:appMenu];
	[((NSMenu*)nsMenu) addItem:appMenuItem];
	[appMenuItem release];
	[appMenu release];
}

@implementation NSApplication (DOSBoxX)
- (void)DOSBoxXMenuAction:(id)sender
{
	/* sorry! */
	mainMenu.mainMenuAction([sender tag]);
}
@end
#endif

