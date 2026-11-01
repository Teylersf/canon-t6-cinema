# Narrow read-only T6 camera information client

The read-only protocol path was tested successfully on the connected Canon EOS
Rebel T6. It reported firmware `3-1.1.0`, corresponding to Canon firmware 1.1.0.
Building this utility and running `--self-test` do not access hardware. See the
[sanitized hardware baseline](../../docs/HARDWARE_BASELINE.md) for the bounded
result and what remains untested.

The only permitted PTP operation is **GetDeviceInfo, 0x1001**, with no parameters.
It reports manufacturer, model, firmware and supported-operation/property codes.
It does not read or enumerate photographs, change camera properties, invoke
EventProc, change remote/event modes, or change boot flags. No generic operation
code, property-write, upload, driver installation or firmware function exists.

It uses the same native Windows WPD transport approach audited in camlib/libwpd,
implemented directly against the installed Windows SDK to avoid linking the
full library's vendor-write operations and POSIX build dependencies. Windows
manages the underlying PTP sessions; this tool does not transmit OpenSession or
CloseSession. It opens exactly one matching `VID_04A9&PID_32B4` WPD device with
`GENERIC_READ` access, sends a device-to-host data command, reads the
result and ends that transfer before closing the WPD connection. No driver
changes are needed. WPD may deny standard-operation passthrough; failure is
reported without falling back to vendor operations or broadening access.

Both access layers explicitly request read permission: the WPD handle uses
`GENERIC_READ`, and each command sets `WPD_API_OPTION_IOCTL_ACCESS` to
`FILE_READ_ACCESS`. The initial version omitted the request-level IOCTL option
and received `E_ACCESSDENIED`; adding the documented read-access option made the
default read-only query succeed on this camera. No broader-access mode or
automatic permission fallback remains. The only PTP opcode is GetDeviceInfo
(`0x1001`), with an empty parameter list and a device-to-host data phase. The
redacted JSON identifies both requested access settings.

The complete WPD command allowlist is:

- `WPD_COMMAND_MTP_EXT_EXECUTE_COMMAND_WITH_DATA_TO_READ` with PTP `0x1001` only.
- `WPD_COMMAND_MTP_EXT_READ_DATA` for the returned transfer context.
- `WPD_COMMAND_MTP_EXT_END_DATA_TRANSFER` to complete the same read transfer.

Limits: at most 128 WPD device identifiers, exactly one matching camera, 64 KiB
DeviceInfo payload, 4 KiB read batches, at most 2,048 entries per reported array.
Malformed/truncated payloads and failed reads abort. No automatic retries.
COM driver calls may block; there is no claimed hard driver-level timeout.

The serial-number field is skipped during parsing and replaced with
`[redacted]` in output. PnP IDs, transfer-context IDs and raw payloads are never
printed or written to files. Application-owned payload bytes are wiped after
use; Windows/COM may retain their own internal copies while servicing a request.
Use only synthetic inputs in committed tests or logs.

## Build and offline test

Run `build.cmd` with Visual Studio 2022 Build Tools and the Windows 10/11 SDK
installed, then run:

```powershell
.\build\t6-camera-info.exe --self-test
```

The default invocation prints help without initializing COM. An actual query
requires the explicit query flag:

```powershell
.\build\t6-camera-info.exe --query-device-info
```

Execute a hardware query only after reviewing the read-access flags and the
fixed request code. The client prints redacted JSON to stdout and does not
create a device-data file.

## Primary-source audit

- camlib checkout: `f1c94c68a1fa0671feccf371001b74a7275b3bd7`.
  [DeviceInfo example](https://github.com/petabyt/camlib/blob/f1c94c68a1fa0671feccf371001b74a7275b3bd7/examples/info.c),
  [standard operation definitions](https://github.com/petabyt/camlib/blob/f1c94c68a1fa0671feccf371001b74a7275b3bd7/src/operations.c#L87),
  [Windows WPD adapter](https://github.com/petabyt/camlib/blob/f1c94c68a1fa0671feccf371001b74a7275b3bd7/src/libwpd.c).
- libwpd checkout: `1b19538ab436339b23736fa2e7b536077a524bc7`.
  [Native command/data flow](https://github.com/petabyt/libwpd/blob/1b19538ab436339b23736fa2e7b536077a524bc7/src/main.cpp#L322),
  [Windows-owned session handling](https://github.com/petabyt/libwpd/blob/1b19538ab436339b23736fa2e7b536077a524bc7/src/main.cpp#L580).
- Installed owning SDK declarations inspected:
  `Windows Kits/10/Include/10.0.22621.0/um/WpdMtpExtensions.h` and
  `PortableDevice.h`. The transfer's total byte count is `VT_UI8`; client access
  is `WPD_CLIENT_DESIRED_ACCESS`. The code uses the SDK's native types.
- [Microsoft client-access documentation](https://learn.microsoft.com/en-us/windows/win32/wpd_sdk/client-properties)
  describes `WPD_CLIENT_DESIRED_ACCESS`; this utility explicitly requests
  `GENERIC_READ` rather than accepting the broader API default. Its
  [SendCommand documentation](https://learn.microsoft.com/en-us/windows/win32/api/portabledeviceapi/nf-portabledeviceapi-iportabledevice-sendcommand)
  describes `WPD_API_OPTION_IOCTL_ACCESS=FILE_READ_ACCESS`, which this utility
  sets on every allowed command. SDK types and keys are used directly.

GPL-2.0; see the repository LICENSE. This source does not incorporate the
third-party library implementations or redistribute their binaries.
