# Translating LibreConnect

LibreConnect uses Qt's translation system. English is the source language. Translators only need to edit the `.ts` files under `translations/`; source strings stay in English.

## Current coverage

The desktop app UI is marked for translation, including:

- all desktop QML pages and dialogs;
- desktop controller status and error messages;
- file-manager type and filter labels;
- the desktop tray menu (`Show`, `Quit`);
- `LanguageController`'s language picker labels;
- the virtual-camera unavailable reason from `SystemInfo`.

System-provided error text (for example, the message produced by `std::error_code::message()`) stays in the operating system's language. Module failure-reason labels can still appear in English inside module error messages.

The mobile app and Android-native strings are not marked yet. They will be added in follow-up changes.

## Marking strings

- QML: wrap user-visible text in `qsTr()`, for example `text: qsTr("Settings")`.
- C++ classes with `Q_OBJECT`: use `tr()`.
- Other C++ code: use `QCoreApplication::translate("Context", "...")`, with a stable context name.
- Do not translate log messages, protocol field names, JSON keys, file paths, command-line option names, or identifiers.
- Keep placeholders such as `%1` and `%n` exactly as they are.

## Adding or updating a translation

1. Copy an existing `.ts` file, for example:

   ```text
   translations/libreconnect_desktop_zh_CN.ts
   ```

2. Set the `language` attribute in the new file to the target locale, for example `de` or `pt_BR`.

3. Add the file to the `qt_add_translations()` call in `apps/desktop/CMakeLists.txt`.

4. Update the source strings and line numbers with the Qt Linguist tools:

   ```bash
   cmake --build <build-directory> --target update_translations
   ```

   Alternatively, run `lupdate` directly on the source directories.

5. Translate the messages in Qt Linguist or in the XML file. Keep the `<source>` text unchanged.

6. Build the project. The `release_translations` target compiles the `.ts` files into `.qm` files and embeds them under the `:/i18n` Qt resource prefix.

## Using a translation at runtime

The language is stored in `QSettings` under `ui/language`. An empty value means "follow the system locale".

Users select a language in **Settings > Language**. The change is applied after restarting the app; the settings page shows a reminder when a restart is pending. On startup, `LanguageController` picks the best available translation for the saved language or the system locale, and falls back to English when no translation exists.

## Testing

- Start with the system locale set to the target language and verify the pilot pages.
- Use the language selector in Settings, restart, and verify the same pages again.
- Check that labels do not clip at the default window size, especially for longer translations.
- Run `lrelease` and confirm it reports `0 unfinished` for the target file.

## License

Translation contributions are accepted under the same GNU GPL v3.0 license as the rest of the project.
