/** @file
  Produces Keyboard Info Protocol.

  Copyright (C) 2016 CupertinoNet.  All rights reserved.<BR>

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

STATIC APPLE_PLATFORM_INFO_DATABASE_PROTOCOL *mPlatformInfo = NULL;

STATIC BOOLEAN mIdsInitialized = FALSE;

STATIC UINT16 mIdVendor = 0;

STATIC UINT16 mIdProduct = 0;

STATIC UINT8 mCountryCode = 0;

STATIC VOID *mAppleKeyMapDbRegistration = NULL;

// mKeyboardInfo
EFI_KEYBOARD_INFO_PROTOCOL mKeyboardInfo = {
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

  ASSERT (UsbKeyboardDevice != NULL);
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

/**
  Protocol installation notify for Apple KeyMap Database.

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

  ASSERT (Event != NULL);
  ASSERT (Context != NULL);
  ASSERT (((USB_KB_DEV *)Context)->Signature == USB_KB_DEV_SIGNATURE);

  Status = gBS->LocateProtocol (
                  &gAppleKeyMapDatabaseProtocolGuid,
                  mAppleKeyMapDbRegistration,
                  (VOID **)&AppleKeyMapDb
                  );
  ASSERT_EFI_ERROR (Status);

  UsbKbSetAppleKeyMapDb ((USB_KB_DEV *)Context, AppleKeyMapDb);

  gBS->CloseEvent (Event);
}

VOID
UsbKbLocateAppleKeyMapDb (
  IN USB_KB_DEV  *UsbKeyboardDevice
  )
{
  EFI_STATUS                      Status;
  APPLE_KEY_MAP_DATABASE_PROTOCOL *AppleKeyMapDb;

  ASSERT (UsbKeyboardDevice != NULL);

  Status = gBS->LocateProtocol (
                  &gAppleKeyMapDatabaseProtocolGuid,
                  NULL,
                  (VOID **)&AppleKeyMapDb
                  );

  if (!EFI_ERROR (Status)) {
    UsbKbSetAppleKeyMapDb (UsbKeyboardDevice, AppleKeyMapDb);
  } else {
    EfiCreateProtocolNotifyEvent (
      &gAppleKeyMapDatabaseProtocolGuid,
      TPL_NOTIFY,
      UsbKbAppleKeyMapDbInstallNotify,
      (VOID *)UsbKeyboardDevice,
      &mAppleKeyMapDbRegistration
      );
  }
}

VOID
UsbKbFreeAppleKeyMapDb (
  IN USB_KB_DEV  *UsbKeyboardDevice
  )
{
  if (UsbKeyboardDevice->KeyMapDb != NULL) {
    UsbKeyboardDevice->KeyMapDb->RemoveKeyStrokesBuffer (
                                   UsbKeyboardDevice->KeyMapDb,
                                   UsbKeyboardDevice->KeyMapDbIndex
                                   );
  }
}

VOID
UsbKbInstallKeyboardDeviceInfoProtocol (
  IN USB_KB_DEV           *UsbKeyboardDevice,
  IN EFI_USB_IO_PROTOCOL  *UsbIo
  )
{
  EFI_DEV_PATH_PTR       DevicePath;
  EFI_GUID               NameGuid;
  UINTN                  Shift;
  UINT32                 Value;
  UINT8                  Index;
  UINTN                  Size;
  UINT32                 Data;
  EFI_STATUS             Status;
  EFI_USB_HID_DESCRIPTOR HidDescriptor;

  DevicePath.DevPath = UsbKeyboardDevice->DevicePath;
  NameGuid           = gApplePlatformInfoKeyboardGuid;
  Shift              = 20;
  Value              = 0;
  Index              = 1;

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

  if (mPlatformInfo == NULL) {
    gBS->LocateProtocol (
           &gApplePlatformInfoDatabaseProtocolGuid,
           NULL,
           (VOID **)&mPlatformInfo
           );
  }

  if (mPlatformInfo != NULL) {
    Status = mPlatformInfo->GetFirstDataSize (mPlatformInfo, &NameGuid, &Size);

    if (!EFI_ERROR (Status) && (Size == sizeof (Data))) {
      Status = mPlatformInfo->GetFirstData (mPlatformInfo, &NameGuid, &Data, &Size);

      if ((Status == EFI_SUCCESS) && (Value == Data) && !mIdsInitialized) {
        Status = UsbGetHidDescriptor (
                   UsbIo,
                   UsbKeyboardDevice->InterfaceDescriptor.InterfaceNumber,
                   &HidDescriptor
                   );

        if (Status == EFI_SUCCESS) {
          mCountryCode = HidDescriptor.CountryCode;
        }

        mIdVendor       = UsbKeyboardDevice->DeviceDescriptor.IdVendor;
        mIdProduct      = UsbKeyboardDevice->DeviceDescriptor.IdProduct;
        mIdsInitialized = TRUE;

        // FIXME: This is never uninstalled.

        gBS->InstallProtocolInterface (
               NULL,
               &gEfiKeyboardInfoProtocolGuid,
               EFI_NATIVE_INTERFACE,
               (VOID *)&mKeyboardInfo
               );
      }
    }
  }
}
