/** @file
  Produces Keyboard Info Protocol.

  Copyright (C) 2016 - 2017, CupertinoNet.  All rights reserved.<BR>

  Licensed under the Apache License, Version 2.0 (the "License");
  you may not use this file except in compliance with the License.
  You may obtain a copy of the License at

  http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.
**/

#include "EfiKey.h"
#include "AppleKey.h"

#include <Library/DevicePathLib.h>
#include <Library/EfiBootServicesLib.h>
#include <Library/MiscEventLib.h>

// ASSERT_USB_KB_DEV_VALID
#define ASSERT_USB_KB_DEV_VALID(UsbKbDev)                                  \
  do {                                                                     \
    ASSERT (UsbKbDev != NULL);                                             \
    ASSERT (((USB_KB_DEV *)UsbKbDev)->Signature == USB_KB_DEV_SIGNATURE);  \
  } while (FALSE)

// mIdVendor
STATIC UINT16 mIdVendor = 0;

// mIdProduct
STATIC UINT16 mIdProduct = 0;

//mCountryCode
STATIC UINT8 mCountryCode = 0;

// mAppleKeyMapDbRegistration
STATIC VOID *mAppleKeyMapDbRegistration = NULL;

// mKeyboardInfo
STATIC EFI_KEYBOARD_INFO_PROTOCOL mKeyboardInfo = {
  UsbKbGetKeyboardDeviceInfo
};

EFI_STATUS
EFIAPI
UsbKbGetKeyboardDeviceInfo (
  OUT UINT16  *IdVendor,
  OUT UINT16  *IdProduct,
  OUT UINT8   *CountryCode
  )
{
  *IdVendor    = mIdVendor;
  *IdProduct   = mIdProduct;
  *CountryCode = mCountryCode;

  return EFI_SUCCESS;
}

STATIC
VOID
UsbKbSetAppleKeyMapDb (
  IN USB_KB_DEV                       *UsbKeyboardDevice,
  IN APPLE_KEY_MAP_DATABASE_PROTOCOL  *AppleKeyMapDb
  )
{
  EFI_STATUS Status;

  ASSERT_USB_KB_DEV_VALID (UsbKeyboardDevice);
  ASSERT (AppleKeyMapDb != NULL);

  Status = AppleKeyMapDb->CreateKeyStrokesBuffer (
                            AppleKeyMapDb,
                            6,
                            &UsbKeyboardDevice->KeyMapDbIndex
                            );

  if (!EFI_ERROR (Status)) {
    UsbKeyboardDevice->KeyMapDb = AppleKeyMapDb;
  }
}

// UsbKbAppleKeyMapDbInstallNotify
/** Protocol installation notify for Apple KeyMap Database.

  @param[in] Event    Indicates the event that invoke this function.
  @param[in] Context  Indicates the calling context.
**/
VOID
EFIAPI
UsbKbAppleKeyMapDbInstallNotify (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  EFI_STATUS                      Status;
  APPLE_KEY_MAP_DATABASE_PROTOCOL *AppleKeyMapDb;
  USB_KB_DEV                      *UsbKeyboardDevice;

  ASSERT (Event != NULL);
  ASSERT_USB_KB_DEV_VALID (Context);
  ASSERT (((USB_KB_DEV *)Context)->KeyMapInstallNotifyEvent == Event);

  Status = EfiLocateProtocol (
             &gAppleKeyMapDatabaseProtocolGuid,
             mAppleKeyMapDbRegistration,
             (VOID **)&AppleKeyMapDb
             );

  ASSERT (Status != EFI_NOT_FOUND);

  UsbKeyboardDevice = (USB_KB_DEV *)Context;

  UsbKbSetAppleKeyMapDb (UsbKeyboardDevice, AppleKeyMapDb);

  EfiCloseEvent (UsbKeyboardDevice->KeyMapInstallNotifyEvent);

  UsbKeyboardDevice->KeyMapInstallNotifyEvent = NULL;
}

// UsbKbLocateAppleKeyMapDb
VOID
UsbKbLocateAppleKeyMapDb (
  IN USB_KB_DEV  *UsbKeyboardDevice
  )
{
  EFI_STATUS                      Status;
  APPLE_KEY_MAP_DATABASE_PROTOCOL *AppleKeyMapDb;

  ASSERT_USB_KB_DEV_VALID (UsbKeyboardDevice);

  Status = EfiLocateProtocol (
             &gAppleKeyMapDatabaseProtocolGuid,
             NULL,
             (VOID **)&AppleKeyMapDb
             );

  if (!EFI_ERROR (Status)) {
    UsbKbSetAppleKeyMapDb (UsbKeyboardDevice, AppleKeyMapDb);
  } else if (PcdGetBool (PcdNotifyAppleKeyMapDbInUsbKbDriver)) {
    UsbKeyboardDevice->KeyMapInstallNotifyEvent = MiscCreateNotifySignalEvent (
                                                    UsbKbAppleKeyMapDbInstallNotify,
                                                    (VOID *)UsbKeyboardDevice
                                                    );

    EfiRegisterProtocolNotify (
      &gAppleKeyMapDatabaseProtocolGuid,
      UsbKeyboardDevice->KeyMapInstallNotifyEvent,
      &mAppleKeyMapDbRegistration
      );
  }
}

// UsbKbFreeAppleKeyMapDb
VOID
UsbKbFreeAppleKeyMapDb (
  IN USB_KB_DEV  *UsbKeyboardDevice
  )
{
  ASSERT_USB_KB_DEV_VALID (UsbKeyboardDevice);

  if (UsbKeyboardDevice->KeyMapDb != NULL) {
    UsbKeyboardDevice->KeyMapDb->RemoveKeyStrokesBuffer (
                                   UsbKeyboardDevice->KeyMapDb,
                                   UsbKeyboardDevice->KeyMapDbIndex
                                   );
  } else if (UsbKeyboardDevice->KeyMapInstallNotifyEvent != NULL) {
    EfiCloseEvent (UsbKeyboardDevice->KeyMapInstallNotifyEvent);
  }
}

// UsbKbInstallKeyboardDeviceInfoProtocol
VOID
UsbKbInstallKeyboardDeviceInfoProtocol (
  IN USB_KB_DEV           *UsbKeyboardDevice,
  IN EFI_USB_IO_PROTOCOL  *UsbIo
  )
{
  STATIC BOOLEAN IdsInitialized = FALSE;

  EFI_DEV_PATH_PTR       DevicePath;
  EFI_GUID               NameGuid;
  UINTN                  Shift;
  UINT32                 Value;
  UINT8                  Index;
  UINTN                  Size;
  UINT32                 Data;
  EFI_STATUS             Status;
  EFI_USB_HID_DESCRIPTOR HidDescriptor;

  ASSERT_USB_KB_DEV_VALID (UsbKeyboardDevice);

  DevicePath.DevPath = UsbKeyboardDevice->DevicePath;
  NameGuid           = gApplePlatformInfoKeyboardGuid;
  Shift              = 20;
  Value              = 0;

  for (Index = 1; Index < 20; Index++) {
    if ((DevicePathType (DevicePath.DevPath) == HARDWARE_DEVICE_PATH)
     && (DevicePathSubType (DevicePath.DevPath) == HW_PCI_DP)) {
      Value = (((UINT32)(DevicePath.Pci->Device & 0x1F)
                | (DevicePath.Pci->Function << 5)) << 24);
    }

    if ((DevicePathType (DevicePath.DevPath) == MESSAGING_DEVICE_PATH)
     && (DevicePathSubType (DevicePath.DevPath) == MSG_USB_DP)) {
      Value |= (((UINT32)DevicePath.Usb->ParentPortNumber + 1) << Shift);
      Shift -= 4;
    }

    if (IsDevicePathEnd (DevicePath.DevPath)) {
      break;
    }
  }

  Size = sizeof (Data);

  if (UsbKeyboardDevice->PlatformInfo == NULL) {
    EfiLocateProtocol (
      &gApplePlatformInfoDatabaseProtocolGuid,
      NULL,
      (VOID **)&UsbKeyboardDevice->PlatformInfo
      );
  }

  if (UsbKeyboardDevice->PlatformInfo != NULL) {
    Status = UsbKeyboardDevice->PlatformInfo->GetFirstDataSize (
                                                UsbKeyboardDevice->PlatformInfo,
                                                &NameGuid,
                                                &Size
                                                );

    if (!EFI_ERROR (Status) && (Size == sizeof (Data))) {
      Status = UsbKeyboardDevice->PlatformInfo->GetFirstData (
                                                  UsbKeyboardDevice->PlatformInfo,
                                                  &NameGuid,
                                                  &Data,
                                                  &Size
                                                  );

      if (!EFI_ERROR (Status) && (Value == Data) && !IdsInitialized) {
        Status = UsbGetHidDescriptor (
                   UsbIo,
                   UsbKeyboardDevice->InterfaceDescriptor.InterfaceNumber,
                   &HidDescriptor
                   );

        if (!EFI_ERROR (Status)) {
          mCountryCode = HidDescriptor.CountryCode;
        }

        mIdVendor      = UsbKeyboardDevice->DeviceDescriptor.IdVendor;
        mIdProduct     = UsbKeyboardDevice->DeviceDescriptor.IdProduct;
        IdsInitialized = TRUE;

        EfiInstallMultipleProtocolInterfaces (
          &gImageHandle,
          &gEfiKeyboardInfoProtocolGuid,
          (VOID *)&mKeyboardInfo,
          NULL
          );
      }
    }
  }
}

// UsbKbUninstallKeyboardDeviceInfoProtocol
VOID
UsbKbUninstallKeyboardDeviceInfoProtocol (
  IN USB_KB_DEV  *UsbKeyboardDevice
  )
{
  ASSERT_USB_KB_DEV_VALID (UsbKeyboardDevice);

  EfiUninstallMultipleProtocolInterfaces (
    gImageHandle,
    &gEfiKeyboardInfoProtocolGuid,
    (VOID *)&mKeyboardInfo
    );
}
