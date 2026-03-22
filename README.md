# Shutdown On Preempt

Initiates proper OS shutdown when the **Azure Spot** Virtual Machine is about to be evicted (deallocated).

The eviction notice is typically given 30 seconds in advance.
The shutdown is initiated immediately with 10 second countdown.

Implemented as a simple lightweight native service process to replace scripts,
which are CPU and resource heavier; significantly in comparison.

## Download prebuilt EXEs

* **x86-64**:
 [ShutdownOnPreempt.exe](https://github.com/tringi/shutdown-on-preempt/raw/refs/heads/master/Builds/x64/Release/ShutdownOnPreempt.exe) &
 [Installer.exe](https://github.com/tringi/shutdown-on-preempt/raw/refs/heads/master/Builds/x64/Release/Installer.exe)
* **x86-32**:
 [ShutdownOnPreempt.exe](https://github.com/tringi/shutdown-on-preempt/raw/refs/heads/master/Builds/Win32/Release/ShutdownOnPreempt.exe) &
 [Installer.exe](https://github.com/tringi/shutdown-on-preempt/raw/refs/heads/master/Builds/Win32/Release/Installer.exe)
* **AArch64**:
 [ShutdownOnPreempt.exe](https://github.com/tringi/shutdown-on-preempt/raw/refs/heads/master/Builds/ARM64/Release/ShutdownOnPreempt.exe) &
 [Installer.exe](https://github.com/tringi/shutdown-on-preempt/raw/refs/heads/master/Builds/ARM64/Release/Installer.exe)

## Requirements

* Runs on all supported Windows and Windows Server operating systems
* Uses about 1.5 MB of RAM and barely CPU cycles
* Polls Azure endpoint every second

## Installation

Run `Installer.exe` and click *"Install and start service"*.

The installer will:

* Copy the `ShutdownOnPreempt.exe` to `C:\Windows\System32`
* Install that file as a service
* Creates new *"Azure Shutdown On Preempt"* shutdown reason code (0x40aa0101)
* Starts the service immediately

Uninstalling will stop and remove the service, delete the EXE, and remove the shutdown code.

## Monitoring

Run Event Viewer, `eventvwr.exe`, navigate to Windows Logs / System,
and search for Event ID **1074** from "User32" source.

The comment will read "Automatic VM Shutdown on Azure Spot Eviction" if performed by this software.

## Settings

There are no configurable options.

## References

* https://learn.microsoft.com/en-us/azure/virtual-machines/windows/scheduled-events
* https://learn.microsoft.com/en-us/windows/win32/shutdown/system-shutdown-reason-codes#remarks

