// Presentation only. These instructions never select a device or save settings.
export function usbNextStep(op,value){
 if(op==='usb_ports')return value.length?'Choose your Cloud Stick in the USB list, then click “Check this USB”.':'No supported USB found. Leave the stick plugged in, check the cable, then click “Find my USB” again.';
 if(op==='usb_open_setup')return value?.accepted===true?
  'Setup is open for about two minutes. Go to sign-in, click “Find Cloud Stick”, then select the name beginning CSP-W2-. Leave the USB plugged in.':
  'Setup did not open. Finish any current operation and retry. You can also hold the stick’s BOOT button for three seconds, then release it.';
 if(op==='usb_status')return value?.owner_ready===false?
  'This stick has no ready owner login. First-owner enrollment needs a separate guided step; this screen will not overwrite its settings.':
  'USB status received. Have your saved login or existing private key ready below. Then click “Open 2-minute setup”. Wi-Fi connected alone does not prove a verified backup.';
 return '';
}

const USB_FAILURE_HELP=Object.freeze({
 usb_permission_denied:'Linux has not granted this app access to the USB. Install the app’s Linux device-access support, then check the USB again. This is a computer permission issue, not an incorrect login password.',
 usb_busy:'Another program is using this USB connection. Close other setup apps or serial monitors, then click “Check this USB” again. Leave the stick plugged in.',
 usb_busy_unknown:'The app cannot confirm that it has sole access to the USB. Close other setup tools and follow the device-access instructions before retrying. The app will not take over an uncertain connection.',
 usb_timeout:'The stick did not reply in time. Wait for any current operation to finish, then click “Check this USB” again. Leave the stick plugged in while work is active.',
 timeout:'A reply did not arrive in time. Wait for any current operation to finish before trying again. The app will not start an automatic retry.',
 usb_device_changed:'The selected USB connection changed. Click “Find my USB” and choose the stick again before continuing.',
 usb_write_failed:'The USB request could not be sent completely. Wait for current work to finish, then check the USB connection again. No settings save was requested.',
 invalid_usb_response:'The stick’s reply was not recognized. Check that this stick uses compatible Cloud Stick firmware before continuing. No settings save was requested.',
 usb_response_too_large:'The stick’s reply was not recognized. Check that this stick uses compatible Cloud Stick firmware before continuing. No settings save was requested.',
 native_not_ready:'The app’s local connection is still starting. Wait a moment, then try the button again.',
 cancelled:'The operation was cancelled. No settings save was requested.'
});
export function usbFailureHelp(error){
 const code=typeof error?.code==='string'?error.code:'';
 return Object.hasOwn(USB_FAILURE_HELP,code)?USB_FAILURE_HELP[code]:
  'The local operation could not finish. Check the connection and the app’s device-access instructions before trying again. No settings save was requested.';
}
