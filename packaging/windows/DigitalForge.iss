#ifndef MyAppVersion
#define MyAppVersion "0.1.2"
#endif
#define MyAppName "DigitalForge"
#define MyAppExeName "DigitalForge.exe"
#define MyAppPublisher "DigitalForge"

[Setup]
AppId={{2F8F3F7E-9C1A-4E36-8C4A-6E6C0A9B5B3D}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
DefaultDirName={autopf}\{#MyAppName}
DefaultGroupName={#MyAppName}
DisableProgramGroupPage=yes
OutputDir=output
OutputBaseFilename=DigitalForge-Setup-{#MyAppVersion}
SetupIconFile=..\..\resources\app.ico
UninstallDisplayIcon={app}\{#MyAppExeName}
Compression=lzma
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible

[Languages]
Name: "spanish"; MessagesFile: "compiler:Languages\Spanish.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked
Name: "associate"; Description: "Asociar los archivos .dfproj y .dfc con {#MyAppName}"; GroupDescription: "Asociaciones de archivo"

[Files]
Source: "dist\DigitalForge\*"; DestDir: "{app}"; Flags: recursesubdirs createallsubdirs

; Asociacion de las dos extensiones propias. Cada una tiene su ProgID
; independiente (DigitalForge.Project / DigitalForge.Document) para poder
; darles descripciones distintas en el Explorador, y ambos apuntan al mismo
; ejecutable con "%1" - main.cpp abre ese argumento al arrancar.
; Flags uninsdeletekey/uninsdeletevalue dejan el registro limpio al desinstalar.
[Registry]
Root: HKA; Subkey: "Software\Classes\.dfproj"; ValueType: string; ValueName: ""; ValueData: "DigitalForge.Project"; Flags: uninsdeletevalue; Tasks: associate
Root: HKA; Subkey: "Software\Classes\.dfc"; ValueType: string; ValueName: ""; ValueData: "DigitalForge.Document"; Flags: uninsdeletevalue; Tasks: associate

Root: HKA; Subkey: "Software\Classes\DigitalForge.Project"; ValueType: string; ValueName: ""; ValueData: "Proyecto de DigitalForge"; Flags: uninsdeletekey; Tasks: associate
Root: HKA; Subkey: "Software\Classes\DigitalForge.Project\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: "{app}\{#MyAppExeName},0"; Tasks: associate
Root: HKA; Subkey: "Software\Classes\DigitalForge.Project\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\{#MyAppExeName}"" ""%1"""; Tasks: associate

Root: HKA; Subkey: "Software\Classes\DigitalForge.Document"; ValueType: string; ValueName: ""; ValueData: "Documento de DigitalForge"; Flags: uninsdeletekey; Tasks: associate
Root: HKA; Subkey: "Software\Classes\DigitalForge.Document\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: "{app}\{#MyAppExeName},0"; Tasks: associate
Root: HKA; Subkey: "Software\Classes\DigitalForge.Document\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\{#MyAppExeName}"" ""%1"""; Tasks: associate

; Registra la app en "Abrir con" y en Programas predeterminados, para que
; Windows la ofrezca aunque el usuario no marque la tarea de asociacion.
Root: HKA; Subkey: "Software\Classes\Applications\{#MyAppExeName}\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\{#MyAppExeName}"" ""%1"""; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\Applications\{#MyAppExeName}\SupportedTypes"; ValueType: string; ValueName: ".dfproj"; ValueData: ""; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\Applications\{#MyAppExeName}\SupportedTypes"; ValueType: string; ValueName: ".dfc"; ValueData: ""

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"
Name: "{group}\Desinstalar {#MyAppName}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "{cm:LaunchProgram,{#MyAppName}}"; Flags: nowait postinstall skipifsilent
