# Builds Nexora's per-user MSI from the deployed dist folder with WiX Toolset v3.
# Invoked by the `installer` target:
#   cmake -DDIST_DIR=... -DOUT_DIR=... -DVERSION=... -DWIX_BIN=... -DICON=... -P make_installer.cmake
#
# Per-user MSIs need an HKCU registry value as each component's key path (ICE38/ICE43)
# and RemoveFolder entries for every created folder (ICE64), so components are written
# here, one per directory, rather than harvested with heat.

foreach(var DIST_DIR OUT_DIR VERSION WIX_BIN ICON)
    if(NOT DEFINED ${var})
        message(FATAL_ERROR "make_installer.cmake: ${var} is not set")
    endif()
endforeach()

# Stable across releases; MajorUpgrade replaces older versions with the same UpgradeCode.
set(UPGRADE_CODE "6F7C3A52-1D4E-4B8F-9A2C-5E0B7D31C9A4")
set(GUID_NAMESPACE "2B9E6C41-7F03-4D5A-8E1B-C6A9D0F43E72")
set(REG_KEY "Software\\Nexora\\Installer")

file(GLOB_RECURSE files RELATIVE "${DIST_DIR}" "${DIST_DIR}/*")
list(SORT files)
if(NOT files)
    message(FATAL_ERROR "No files in ${DIST_DIR}; build the deploy target first")
endif()

function(make_id out prefix text)
    string(MAKE_C_IDENTIFIER "${text}" id)
    string(MD5 hash "${text}")
    string(SUBSTRING "${hash}" 0 8 hash)
    set(${out} "${prefix}_${id}_${hash}" PARENT_SCOPE)
endfunction()

# Group files by their directory relative to the install root ("" = root).
set(dirs "")
foreach(file IN LISTS files)
    get_filename_component(dir "${file}" DIRECTORY)
    list(APPEND dirs "${dir}")
    make_id(dir_key "d" "${dir}")
    list(APPEND "files_${dir_key}" "${file}")
endforeach()
list(REMOVE_DUPLICATES dirs)

# Directory tree. Only one level of subfolders is produced by windeployqt.
set(directory_xml "")
foreach(dir IN LISTS dirs)
    if(dir STREQUAL "")
        continue()
    endif()
    if(dir MATCHES "/")
        message(FATAL_ERROR "Nested folder ${dir} is not supported by make_installer.cmake")
    endif()
    make_id(dir_id "DIR" "${dir}")
    string(APPEND directory_xml "          <Directory Id=\"${dir_id}\" Name=\"${dir}\" />\n")
endforeach()

# One component per directory: its files, an HKCU key path and folder cleanup.
set(component_xml "")
set(component_refs "")
foreach(dir IN LISTS dirs)
    make_id(dir_key "d" "${dir}")
    if(dir STREQUAL "")
        set(dir_id "INSTALLFOLDER")
    else()
        make_id(dir_id "DIR" "${dir}")
    endif()
    make_id(cmp_id "CMP" "${dir}")
    string(UUID cmp_guid NAMESPACE "${GUID_NAMESPACE}" NAME "component:${dir}" TYPE SHA1 UPPER)

    string(APPEND component_xml "    <Component Id=\"${cmp_id}\" Directory=\"${dir_id}\" Guid=\"${cmp_guid}\">\n")
    string(APPEND component_xml "      <RegistryValue Root=\"HKCU\" Key=\"${REG_KEY}\" Name=\"${cmp_id}\" Type=\"integer\" Value=\"1\" KeyPath=\"yes\" />\n")
    foreach(file IN LISTS "files_${dir_key}")
        make_id(file_id "FIL" "${file}")
        file(TO_NATIVE_PATH "${DIST_DIR}/${file}" source)
        get_filename_component(name "${file}" NAME)
        string(APPEND component_xml "      <File Id=\"${file_id}\" Name=\"${name}\" Source=\"${source}\" />\n")
    endforeach()
    make_id(rm_id "RM" "${dir}")
    string(APPEND component_xml "      <RemoveFolder Id=\"${rm_id}\" Directory=\"${dir_id}\" On=\"uninstall\" />\n")
    string(APPEND component_xml "    </Component>\n")
    string(APPEND component_refs "      <ComponentRef Id=\"${cmp_id}\" />\n")
endforeach()

string(UUID shortcut_guid NAMESPACE "${GUID_NAMESPACE}" NAME "component:shortcuts" TYPE SHA1 UPPER)
string(UUID autostart_guid NAMESPACE "${GUID_NAMESPACE}" NAME "component:autostart" TYPE SHA1 UPPER)
string(UUID programs_guid NAMESPACE "${GUID_NAMESPACE}" NAME "component:programs-folder" TYPE SHA1 UPPER)
file(TO_NATIVE_PATH "${ICON}" icon_source)

file(MAKE_DIRECTORY "${OUT_DIR}")
set(wxs "${OUT_DIR}/Nexora.wxs")
file(WRITE "${wxs}" "<?xml version=\"1.0\" encoding=\"utf-8\"?>
<Wix xmlns=\"http://schemas.microsoft.com/wix/2006/wi\">
  <Product Id=\"*\" Name=\"Nexora\" Language=\"1033\" Version=\"${VERSION}\" Manufacturer=\"Nexora\"
           UpgradeCode=\"${UPGRADE_CODE}\">
    <Package InstallerVersion=\"500\" Compressed=\"yes\" InstallScope=\"perUser\" InstallPrivileges=\"limited\"
             Description=\"Nexora application launcher\" />
    <MajorUpgrade DowngradeErrorMessage=\"A newer version of Nexora is already installed.\" />
    <MediaTemplate EmbedCab=\"yes\" />

    <Icon Id=\"NexoraIcon\" SourceFile=\"${icon_source}\" />
    <Property Id=\"ARPPRODUCTICON\" Value=\"NexoraIcon\" />
    <Property Id=\"ARPNOMODIFY\" Value=\"1\" />
    <Property Id=\"ARPCOMMENTS\" Value=\"A minimal floating application launcher. Press Ctrl+Space to open.\" />

    <Directory Id=\"TARGETDIR\" Name=\"SourceDir\">
      <Directory Id=\"LocalAppDataFolder\">
        <Directory Id=\"LocalProgramsFolder\" Name=\"Programs\">
          <Directory Id=\"INSTALLFOLDER\" Name=\"Nexora\">
${directory_xml}          </Directory>
        </Directory>
      </Directory>
      <Directory Id=\"ProgramMenuFolder\" />
    </Directory>

${component_xml}
    <Component Id=\"CMP_ProgramsFolder\" Directory=\"LocalProgramsFolder\" Guid=\"${programs_guid}\">
      <RegistryValue Root=\"HKCU\" Key=\"${REG_KEY}\" Name=\"CMP_ProgramsFolder\" Type=\"integer\" Value=\"1\" KeyPath=\"yes\" />
      <RemoveFolder Id=\"RM_ProgramsFolder\" Directory=\"LocalProgramsFolder\" On=\"uninstall\" />
    </Component>

    <Component Id=\"CMP_Shortcuts\" Directory=\"ProgramMenuFolder\" Guid=\"${shortcut_guid}\">
      <Shortcut Id=\"StartMenuShortcut\" Name=\"Nexora\" Description=\"Open the Nexora launcher\"
                Target=\"[INSTALLFOLDER]Nexora.exe\" WorkingDirectory=\"INSTALLFOLDER\" />
      <RegistryValue Root=\"HKCU\" Key=\"${REG_KEY}\" Name=\"CMP_Shortcuts\" Type=\"integer\" Value=\"1\" KeyPath=\"yes\" />
    </Component>

    <!-- Same value the tray's \"Start with Windows\" toggle writes, so the two stay in sync
         and uninstalling removes it. -->
    <Component Id=\"CMP_Autostart\" Directory=\"INSTALLFOLDER\" Guid=\"${autostart_guid}\">
      <RegistryValue Root=\"HKCU\" Key=\"Software\\Microsoft\\Windows\\CurrentVersion\\Run\" Name=\"Nexora\"
                     Type=\"string\" Value=\"&quot;[INSTALLFOLDER]Nexora.exe&quot; --background\" KeyPath=\"yes\" />
    </Component>

    <Feature Id=\"Main\" Title=\"Nexora\" Level=\"1\" Absent=\"disallow\">
${component_refs}      <ComponentRef Id=\"CMP_ProgramsFolder\" />
      <ComponentRef Id=\"CMP_Shortcuts\" />
    </Feature>
    <Feature Id=\"Autostart\" Title=\"Start with Windows\" Level=\"1\">
      <ComponentRef Id=\"CMP_Autostart\" />
    </Feature>
  </Product>
</Wix>
")

set(wixobj "${OUT_DIR}/Nexora.wixobj")
set(msi "${OUT_DIR}/Nexora-${VERSION}-x64.msi")

execute_process(COMMAND "${WIX_BIN}/candle.exe" -nologo -arch x64 -out "${wixobj}" "${wxs}"
                RESULT_VARIABLE result)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "candle failed (${result})")
endif()

# ICE91 only warns that per-user files won't be redirected for other users, which is the intent.
execute_process(COMMAND "${WIX_BIN}/light.exe" -nologo -sice:ICE91 -out "${msi}" "${wixobj}"
                RESULT_VARIABLE result)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "light failed (${result})")
endif()

message(STATUS "Installer: ${msi}")
