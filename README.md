## Launch Menu
*A program and document launching menu based on classical standards*<img width="507" height="629" alt="Picture" src="https://github.com/user-attachments/assets/189b8850-5fe7-49b0-b42a-2351c268ce58" />

### What’s the purpose?
I wanted to have a menu on the top-left of the screen that would allow me to load programs and documents just as in Classic Mac OS, with customizable items. Linux menu applets tend to use automatic menus which conform to the FreeDesktop standard, which typically involves submenus and may not be suited to your muscle memory. The Xfce Applications Menu supports custom menu files and can be made into an approximation of this (which I have done, in my old gMenu), but it doesn’t do it automatically, and it doesn’t handle documents.
### How does it work?
After you add the applet to Xfce4 Panel for the first time, the launch menu icon will appear somewhere on the panel. After clicking on it, a drop-down menu will come up. If you have Hardinfo installed, the first entry will be “About This Computer”, with a link to that program and a separator beneath it. Afterwards, the contents of the Launch Items folder, which is generated in ~/.config, are listed in alphabetical order.

Desktop files are treated as applications, documents are treated as documents, and folders have submenus that allow you to view their contents. Each submenu has an “Open This Folder” item and separator at the top; then an alphabetically arranged list of contents. Submenus go two levels deep. (More than that and it becomes unergonomic.)

The applet is automatically configured with a link to Xfce’s Settings Manager. It may also contain two special folders, Recent Applications and Recent Documents, which are populated with the latest items you loaded that the system detected.
### Are there preferences to set?
Yes, four, all of which pertain to the recent items features.

**Enable Recent Applications and Documents**

With this on, the applet tracks application and document loadings. Applications are detected via wnck and a proc check; documents are detected by reading the recently-used.xbel file. If this is unmarked, this tracking is disabled.

**Classic Duplicate Handling**

There’s the possibility that you may have documents with the same name. With this checked, the Recent Documents list will only show the most recent document with this name. With it unchecked, files with identical names receive their own submenu folders; the files inside bear the names of the folders they are contained in.

**Max recent applications**

How many recent applications are stored in the list.

**Max recent documents**

How many recent documents are stored in the list.
### What’s compatibility like?
Xfce, and also MATE, at this time, are designed to work primarily with X11. This applet’s app tracking management features are also designed to work with X11, via wnck. It does not support and is not tested with Wayland, and there is no reason to think it would work at all. (If it somehow does, it will not be because of anything that I did.)

I’ve built and run it on Fedora 42, and Debians 12 and 13. It might well work on other distributions, provided they have the appropriate dependencies. If you use another distro and are especially interested in getting this, but the build doesn’t work and you don’t have the experience to tweak it yourself, let me know and I’ll see if I can help.
### How do I install it?
There are two .deb and one .rpm files, which have been 
tested in Debian 12, 13, and Fedora 42, respectively and should be 
working correctly.

If you want to build from source, the way I’ve done it is 
to extract the files into a new folder, and in the terminal, enter “make
 && sudo make install”, to install the applet to the root 
system.

Either way, after installation, the applet should show up 
in Xfce Panel's 'Add New Items'. If not, restart the panel (via 
xfce4-panel -r) and look again.

Afterwards, for optimal usability, you need to add some applications, documents or folders to the Launch Menu Items folder (which is stored in ~/.config). Launch Menu will recognize most things you’re likely to put into it, including AppImages, binaries, .desktop files, documents, folders, and symlinks. I recommend using links for as many things as possible.

Application .desktop files can be found in /usr/share/applications. With Thunar this is a relatively simple process (you can create a link by using ‘send to desktop’, and then move the new link into the Launch folder), but Caja makes it difficult to create links to restricted folders. 

Personally, I used Caja Actions to create a new command, “Make Link on Desktop”, with the following params:

*Path: ln*

*Parameters: -s %f/home/username/Desktop/*

*Working directory: %d*

(If you end up copying the desktop files directly in this case, I can’t say I blame you.)

### Are there any other features?
The applet attempts to detect your default file manager, and skips over including it in Recent Applications.

You can add separators to the menu by creating a file of any type that ends with the character '|'. This reimplements the functionality of the old DividerLines extension, except with the symbol changed (I felt the dollar sign was likely to be too common on these systems).
### How is this different from what’s already out there?
The stock Xfce “Application Menu” applet is the closest competitor, although there are others (MATE’s famous Application/Places/System menu, and the Whisker Menu, which is like the Windows start menu).

• Launch Menu relies on manually-organized contents in the Launch Items folder. Application Menu, and other menus, are based on the FreeDesktop Standard, and uses automatically generated menus, which may or may not conform to your expectations. (They do tend to require submenus.)

• Launch Menu can display documents and folders, and handles symlinks (aliases) to them as well. MATE’s application/places/system menu supports a few preselected directories, and recent documents, but it doesn’t support the same range of options as Launch Menu. Application Menu and Whisker Menu have no support for documents or directories.
### When would I use this over the competition?
You will probably prefer this applet if you like the ability to manually arrange things, and have them stay that way. My experience is that the competition is inferior in this respect. They all rely on modifying desktop files, in a destructive process, with glitchy menu editors, where the final results may easily be overwritten or undone, and in any case aren’t easily transferred from system to system.

If you are a fan of Classic Macintoshes and want something that reproduces that functionality, this is your best option, that I’m aware of, that works with a mainstream desktop environment.
### When would I not prefer this?
If you don’t care about manually organizing programs, documents or folders, and/or you like automatically populated menus, you have no reason to use this applet.

Whisker Menu has a search textbox for quick keyboard-driven program access. Launch Menu doesn’t currently support this feature and is currently mouse-driven overall.

If you used Classic Macs before and hated the Apple menu for some reason, this applet won’t do anything that will make you like it any better.
### Is there anything I should be concerned about?
The Recent Applications and Recent Documents folders are ‘magic’, in that they are generated by the applet and regularly updated and flushed by it. They’re not intended or expecting to be used to store your files. If you put foreign files or folders in there, they will likely be destroyed.

~

With recent items tracking turned on, this applet does watch you. Specifically, it relies on wnck (which is why there’s no Wayland support) and the proc directory to detect programs being loaded and to get their proper names, and on the recently-used.xbel file to detect documents. If that bothers you, you’re free to look through the code and verify that there are no network connections or anything else being done that is under-board. Alternatively, you can leave the recent items feature off, which also will (if I coded it right) turn this tracking off.

~

It also checks to see if hardinfo2 (or hardinfo) are installed, so it can provide the “About My Computer” link at the top. This is currently not optional. I suppose this could be done via desktop files, though. Perhaps this will change in a later release.

~

Finally, I’m not a C programmer. I’ve done work in other languages, but I’m a C and GTK novice, at best. The bulk of this applet’s code was produced by an AI tool, namely Anthropic’s Claude (usually the Sonnet 4 model).

This was not a mindless process. I had specific goals in mind, gave prompts with achieving those goals in mind, rejected or threw out suggestions, and made other tweaks and changes where I determined the bot missed the mark. I worked through build errors and debugged logical problems that the bot missed, on multiple occasions. I have personally evaluated the functionality of the applet to make sure it does what I intended. I was the manager, project lead, designer, and tester, and I was a programmer who inspected, tweaked and rearranged the code.

But even so, I technically didn’t originally write most of it. That may bother you. If it does, you are, again, free to test the applet and inspect the codebase yourself and see that it does what it ought to be doing. If you think there’s an area where the performance could be improved, make the tweaks in a fork and send a pull request. I will inspect it and think through whether the change should be made – the same as I did for the bot code – and accept it if it is correct.

Would it be nice if this applet had been made solely by human ability? Yes. But I don’t have the background in C or GTK’s libraries, or in dealing with xfconf or Xfce Panel’s expectations, and I don’t have the patience to take the time to learn all of them and work up to making this when this project has no apparent profit potential. If anyone else was going to make this, they would have done it by now. So the choice here isn’t between an applet with AI code and an applet by an elite human programmer, it’s a choice between an applet and no applet.

### Are there any known bugs or limitations?
With “Classic Duplicate Handling” turned off, a folder may be generated that appears to only have one item in it. This is likely because there’s a file from a hidden directory in that folder. In this case, you have to open the folder manually and ‘view hidden files’ in your file manager. I judged that this defect should not seriously impact anyone except possibly for Linux ‘power users’, who are also capable of working around it, but it’s something to look to fixing when I make the time.

Other new issues I've found have been made issues here on GitHub.

### Why the license?
Although GPL v2 isn’t my favorite, culture and the ecosystem seemed to lend themselves to it; it is popular among Xfce applets. The decision to use AI tools also contributed, given that I’m not entirely sure what its sources were, and that given the project context, it seems possible at least some of it could have originally been inspired by GPL code. I feel much of the code that I have reviewed wouldn’t be reasonably implemented in any other way, but it’s better to be safe here.
This is not a great inconvenience, given the applet’s purpose and audience. Given its intimate access to the system, free access to the source code should help maintain user trust, and I feel all Classic Mac fans should have access to these features if they want them.
