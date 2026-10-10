; MPC-Bridge per-user installer. The GPL license and upstream notices are bundled.
#if VER < EncodeVer(6,5,2)
  #error Inno Setup 6.5.2 or newer is required
#endif
#ifndef PackageDir
  #error Define PackageDir as the verified portable package directory
#endif
#ifndef OutputDir
  #error Define OutputDir
#endif
#ifndef BridgeVersion
  #error Define BridgeVersion
#endif
#ifndef FileVersion
  #error Define FileVersion as a four-part numeric version
#endif

[Setup]
AppId={{A3B0657C-8CA7-4EB2-B3A7-8DA6040BEA18}
AppName=MPC-Bridge
AppVersion={#BridgeVersion}
AppVerName=MPC-Bridge {#BridgeVersion}
AppPublisher=MPC-Bridge contributors
AppPublisherURL=https://github.com/JiaqiZhang-NJU/MPC-Bridge
AppSupportURL=https://github.com/JiaqiZhang-NJU/MPC-Bridge/issues
AppUpdatesURL=https://github.com/JiaqiZhang-NJU/MPC-Bridge/releases
DefaultDirName={localappdata}\Programs\MPC-Bridge
DefaultGroupName=MPC-Bridge
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
OutputDir={#OutputDir}
OutputBaseFilename=MPC-Bridge-{#BridgeVersion}-x64-Setup
VersionInfoVersion={#FileVersion}
VersionInfoProductTextVersion={#BridgeVersion}
VersionInfoDescription=MPC-Bridge Setup
SetupIconFile=..\..\src\apps\mplayerc\res\icon.ico
UninstallDisplayIcon={app}\MPC-Bridge.exe
UninstallDisplayName=MPC-Bridge {#BridgeVersion}
WizardStyle=modern
DisableWelcomePage=yes
DisableDirPage=yes
DisableProgramGroupPage=yes
DisableReadyPage=no
ShowLanguageDialog=no
DisableStartupPrompt=yes
CloseApplications=no
RestartApplications=no
Compression=lzma2/max
SolidCompression=yes
UsePreviousAppDir=yes

[Languages]
Name: en; MessagesFile: compiler:Default.isl
Name: sc; MessagesFile: ..\..\distrib\Languages\ChineseSimplified.isl

[CustomMessages]
en.LaunchBridge=Start MPC-Bridge
sc.LaunchBridge=启动 mpc-bridge
en.ReadyMemo=Installs the player and online video parser for your Windows account, with desktop and Start menu shortcuts. Existing settings are kept when upgrading. This is a preview release.
sc.ReadyMemo=为当前用户安装播放器和在线视频解析器，并创建桌面与开始菜单快捷方式。升级时保留已有设置。这是预览版本。

[Files]
Source: "{#PackageDir}\*"; DestDir: "{app}"; Excludes: "\MPC-Bridge.ini"; Flags: ignoreversion recursesubdirs createallsubdirs
; Configuration belongs to the user: never overwrite it or remove it on uninstall.
Source: "{#PackageDir}\MPC-Bridge.ini"; DestDir: "{app}"; Flags: onlyifdoesntexist uninsneveruninstall

[Icons]
Name: "{autoprograms}\MPC-Bridge\MPC-Bridge"; Filename: "{app}\MPC-Bridge.exe"; Parameters: "/new"; WorkingDir: "{app}"
Name: "{autoprograms}\MPC-Bridge\Uninstall MPC-Bridge"; Filename: "{uninstallexe}"
Name: "{autodesktop}\MPC-Bridge"; Filename: "{app}\MPC-Bridge.exe"; Parameters: "/new"; WorkingDir: "{app}"

[Run]
Filename: "{app}\MPC-Bridge.exe"; Parameters: "/new"; WorkingDir: "{app}"; Description: "{cm:LaunchBridge}"; Flags: nowait postinstall skipifsilent

[Code]
var
  HadConfiguration: Boolean;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep = ssInstall then
    HadConfiguration := FileExists(ExpandConstant('{app}\MPC-Bridge.ini'));
  if (CurStep = ssPostInstall) and (not HadConfiguration) then
    SetIniString('Settings', 'Language', ActiveLanguage, ExpandConstant('{app}\MPC-Bridge.ini'));
end;

function UpdateReadyMemo(Space, NewLine, MemoUserInfoInfo, MemoDirInfo, MemoTypeInfo,
  MemoComponentsInfo, MemoGroupInfo, MemoTasksInfo: String): String;
begin
  Result := CustomMessage('ReadyMemo') + NewLine + NewLine + MemoDirInfo;
end;
