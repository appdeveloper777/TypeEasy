#ifndef AppVersion
#define AppVersion "0.0.1"
#endif

#ifndef SourceDir
#define SourceDir "..\\..\\dist\\windows\\TypeEasy-0.0.1-win64"
#endif

#ifndef OutputDir
#define OutputDir "..\\..\\dist\\windows"
#endif

[Setup]
AppId={{E60A47DC-5C8F-4D50-A2CF-0B86E8A70517}
AppName=TypeEasy
AppVersion={#AppVersion}
AppPublisher=TypeEasy
DefaultDirName={autopf}\TypeEasy
DefaultGroupName=TypeEasy
OutputDir={#OutputDir}
OutputBaseFilename=TypeEasy-Setup-v{#AppVersion}
Compression=lzma
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=lowest
DisableProgramGroupPage=yes
ChangesEnvironment=yes

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"
Name: "spanish"; MessagesFile: "compiler:Languages\\Spanish.isl"

[Tasks]
Name: "desktopicon"; Description: "Crear acceso directo en el escritorio"; GroupDescription: "Opciones adicionales:"; Flags: unchecked
Name: "addtopath"; Description: "Agregar TypeEasy al PATH (recomendado)"; GroupDescription: "Opciones adicionales:";

[Files]
Source: "{#SourceDir}\\*"; DestDir: "{app}"; Flags: recursesubdirs createallsubdirs ignoreversion

[Icons]
Name: "{autoprograms}\\TypeEasy"; Filename: "{app}\\bin\\typeeasy.exe"; WorkingDir: "{app}"
Name: "{autodesktop}\\TypeEasy"; Filename: "{app}\\bin\\typeeasy.exe"; WorkingDir: "{app}"; Tasks: desktopicon

[Registry]
; Agrega {app}\bin al PATH del usuario para que `typeeasy hola.te` funcione desde cualquier consola.
Root: HKCU; Subkey: "Environment"; ValueType: expandsz; ValueName: "Path"; \
    ValueData: "{olddata};{app}\\bin"; \
    Check: NeedsAddPath(ExpandConstant('{app}\\bin')); Tasks: addtopath

[Run]
Filename: "{cmd}"; Parameters: "/k cd /d ""{app}"" && .\\bin\\typeeasy.exe --help"; Description: "Abrir TypeEasy en consola"; Flags: postinstall nowait skipifsilent

[Code]
function NeedsAddPath(Param: string): Boolean;
var
  OrigPath: string;
begin
  if not RegQueryStringValue(HKEY_CURRENT_USER, 'Environment', 'Path', OrigPath) then
  begin
    Result := True;
    exit;
  end;
  Result := Pos(';' + Lowercase(Param) + ';', ';' + Lowercase(OrigPath) + ';') = 0;
end;
