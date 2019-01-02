#
#    Copyright (C) 2014 Mario Stephan <mstephan@shared-files.de>
#
%define name    knowthelist

%define qmake qmake-qt4

%if 0%{?suse_version}
%define qmake /usr/bin/qmake
%endif
%if 0%{?suse_version} >=1310
%define qmake /usr/%_lib/qt5/bin/qmake
%endif
%if 0%{?fedora_version} >= 20
%define qmake /usr/bin/qmake-qt5
%endif

Summary: awesome party music player
Name: %{name}
License: LGPL-3.0+
URL: https://github.com/knowthelist/knowthelist
Version: 2.3.0
Release: 1
Group: Multimedia
Source: %{name}_%{version}.orig.tar.gz
Packager: Mario Stephan <mstephan@shared-files.de>
Distribution: %{distr}
BuildRoot:      %{_tmppath}/%{name}-%{version}-build

%if 0%{?suse_version} && 0%{?suse_version} <1310
BuildRequires: libqt4-devel >= 4.8 qwt6-devel
BuildRequires: pkgconfig(gstreamer-0.10)
BuildRequires: update-desktop-files
BuildRequires: libtag-devel
Requires:       libqt4-qtbase
Requires:       gstreamer-10-plugins-base
Requires:       gstreamer-10-plugins-ugly
Requires:       gstreamer-10-plugins-good
Requires:       gstreamer-10-plugins-bad
Requires:       libgstreamer-10-0
Requires:       gstreamer-10
%endif
%if 0%{?suse_version} >=1310 
BuildRequires: taglib-devel 
BuildRequires: pkgconfig(gstreamer-1.0)
BuildRequires: libqt5-qtbase-devel
BuildRequires: libqt5-qttools
BuildRequires: update-desktop-files
Requires:       gstreamer-plugins-bad
Requires:       gstreamer-plugins-base
Requires:       gstreamer-plugins-ugly
Requires:       gstreamer-plugins-good
Requires:       gstreamer
Requires:       libqt5-qtbase
%endif
%if 0%{?suse_version} >=1320 
BuildRequires: taglib-devel 
BuildRequires: pkgconfig(gstreamer-1.0)
BuildRequires: libqt5-qtbase-devel
BuildRequires: libqt5-linguist
BuildRequires: update-desktop-files
Requires:       gstreamer-plugins-bad
Requires:       gstreamer-plugins-base
Requires:       gstreamer-plugins-ugly
Requires:       gstreamer-plugins-good
Requires:       gstreamer
Requires:       libqt5-qtbase
%endif
%if 0%{?fedora_version} >= 20
BuildRequires: taglib-devel 
BuildRequires: pkgconfig(gstreamer-1.0)
BuildRequires: qt5-qtbase-devel	
BuildRequires: qt5-qttools-devel
BuildRequires: qt-devel >= 5.0
Requires:       qt5-qtbase
Requires:       gstreamer1-plugins-base
Requires:       gstreamer1-plugins-ugly
Requires:       gstreamer1-plugins-good
Requires:       gstreamer1-plugins-bad-free
Requires:       gstreamer1
%endif
     
BuildRequires: glib2-devel
BuildRequires: gcc-c++
BuildRequires: alsa-devel

Requires:       taglib
Requires:       alsa


%prep
%setup
%build
%{qmake} -makefile %{name}.pro
%{qmake}
make

%install
%{__install} -Dm 755 -s %{name} %{buildroot}%{_bindir}/%{name}
%{__install} -Dm 644 dist/%{name}.desktop %{buildroot}%{_datadir}/applications/%{name}.desktop
%{__install} -Dm 644 dist/%{name}.png %{buildroot}%{_datadir}/pixmaps/%{name}.png
%if 0%{?suse_version} > 0
%suse_update_desktop_file -r %{name} AudioVideo Player
%endif

%clean
[ "%{buildroot}" != "/" ] && %{__rm} -rf %{buildroot}


%files
%defattr(-,root,root,-)
%{_bindir}/*
%{_datadir}/applications/%{name}.desktop
%{_datadir}/pixmaps/%{name}.png



%description
Easy to use for all party guests
Quick search for tracks in collection
Two players with separate playlists
Mixer with fader, 3 channel EQ and gain
Auto fader and auto gain
Trackanalyser search for song start/end and gain setting
Auto DJ function with multiple filters for random play
Monitor player for pre listen tracks (via 2nd sound card e.g. USB)

%changelog
* Sun Sep 19 2014 Mario Stephan <mstephan@shared-files.de>
- 2.3.0
- Made all compatible with Qt5 and Gstreamer-1.0.
- Add an ALL node to filter results in case of a manageable number of tracks are found
- Changed ModeSelector style and moved to tree header
- Included 'year' tag into quick search
* Sun Sep 14 2014 Mario Stephan <mstephan@shared-files.de>
- 2.2.4
- Fixed a bug which prevent correct monitoring of changes
- Improved quick search in collection: added search in genre, multiple
strings
- Changed alignment of some display controls
- Allow more audio file formates
- Added a mutex to get more thread safety for database access
* Tue Aug 26 2014 Mario Stephan <mstephan@shared-files.de>- 2.2.3
- Get rid of dependency to Boost
- Bugfix where adding a song caused a segmentation fault
- Switched to Homebrew package installer for MacOS
- Set CUE button to untranslatable
- Translation updates
* Wed Aug 06 2014 Mario Stephan <mstephan@shared-files.de>
- 2.2.0
- Added a new left side tab "Lists" to manage lists, dynamic and stored lists
- Added a new feature to handle track ratings
- Added a combo box for AutoDJ artist and genre filters to be able to select also from a list
- Added a new way in how to add and remove items of AutoDj and lists
- Added "Open File Location" at playlist context menu
- Added a playlist info label (count,time) to player
- Added French translation (thanks to Geiger David and Adrien D.)
- Changed to a better way to summarise count and length of tracks for AutoDJ
- Optimized for smaller screens
- Fix to be more flexible for empty tags
- Enhanced algorithm to fill playlist and simplified handling of current and next item
- Fixed some size issues and cosmetical issues
- Stabilized to avoid crashed in some cases 

* Thu Jul 03 2014 Mario Stephan <mstephan@shared-files.de>
- 2.1.3
-  Added new widget ModeSelector to select collection tree mode
-  Added a counter for played songs
-  New: Generate a default cover image if the tag provides none
-  Optimized: gain dial moves smoothly now to avoid hard skips of volume
-  Optimized function to decouple database requests from GUI activities
-  Optimized for size scaling of form
* Tue Jun 10 2014 Mario Stephan <mstephan@shared-files.de>
- 2.1.2
- Added translation for hu_HU (thanks to László Farkas)
- AutoDJ panel rearrangements, new record case stack display added
- AutoDJ names settings in settings dialog added  
* Tue Jun 03 2014 Mario Stephan <mstephan@shared-files.de>
- 2.1.1
- Add localization, cs_CZ (thanks to Pavel Fric), de_DE
* Thu May 29 2014 Mario Stephan <mstephan@shared-files.de>
- 2.1.0
- First Release


