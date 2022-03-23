# Translations for alsa-scarlett-gui

Translations are only available after running "make install"
in the src-directory, because the binary mo file needs to be
in the right place.

## create new translations
For creating new translations:
- Add language to the LINGUAS variable in file linguas
- run: make update-po
- edit the new po-file and do the translation
- Go to the parent directory (src) and run: make install
