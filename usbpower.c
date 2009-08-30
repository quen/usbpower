/*
 * usbpower
 * Copyright 2009 Samuel Marshall
 *
 * Utility for issuing USB Suspend and Resume commands on Mac OS X.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdio.h>

#include <mach/mach.h>
#include <CoreFoundation/CFNumber.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/IOCFPlugIn.h>
#include <IOKit/usb/IOUSBLib.h>

#define CHECKRETURN(err, message) if(err) { fprintf(stderr, message " [err %08x]\n", err); return -1; }

/* Converts a string such as '0x0001' to a number. Returns -1 if the string is not valid */
SInt32 convertHexId(const char* arg)
{
	SInt32 result = 0;
	SInt32 i;
	char c;
	
	if(strlen(arg)!=6)
	{
		return -1;
	}
	if(arg[0]!='0' || (arg[1]!='x' && arg[1]!='X'))
	{
		return -1;
	}
	
	for(i=2; i<6; i++)
	{
		result *= 16;
		c = arg[i];
		if(c>='0' && c<='9')
		{
			result += c-'0';
		} 
		else if(c>='a' && c<='f')
		{
			result += 10 + c - 'a';
		}
		else if(c>='A' && c<='F')
		{
			result += 10 + c - 'A';
		}
		else
		{
			return -1;
		}
	}
	
	return result;
}

/* Main method. */
int main (int argc, const char * argv[])
{
	kern_return_t err;
	
	mach_port_t masterPort = 0;
	bool suspend;
	SInt32 productId, vendorId;	
	SInt32 count;
	CFMutableDictionaryRef matcher;
	CFNumberRef numberRef;
	io_iterator_t iterator;
	io_service_t usbService;
    IOCFPlugInInterface **pluginInterface;
	IOUSBDeviceInterface245 **deviceInterface;
	SInt32 score;

	// Display usage info
	if(argc != 4)
	{
		printf("Usage:\n"
			"  usbpower suspend <product id> <vendor id>\n"
			"  usbpower resume <product id> <vendor id>\n"
			"\n"
			"Vendor and product IDs can be obtained by running the command:\n"
			"  system_profiler SPUSBDataType\n"
			"\n"
			"They must be given as four-digit hexadecimal numbers beginning with 0x\n"
			"(as shown by the above command).\n"
			"\n"
			"Example:\n"
			"  usbpower suspend 0x0040 0x045e\n"
			"\n"
			"Copyright 2009 Samuel Marshall - http://www.leafdigital.com/software/\n"
			"Released under Gnu Public License v3.\n");
		return 0;
	}
	
	// Check first parameter
	if(strcmp(argv[1], "suspend") == 0)
	{
		suspend = true;
	}
	else if(strcmp(argv[1], "resume") == 0)
	{
		suspend = false;
	}
	else
	{
		fprintf(stderr, "Invalid argument '%s': expecting suspend, resume\n", argv[1]);
		return -1;
	}
	
	// Check other two parameters
	productId = convertHexId(argv[2]);
	if(productId == -1)
	{
		fprintf(stderr, "Invalid product id '%s': expecting four-digit hexadecimal e.g. 0x0040\n", argv[2]);
		return -1;
	}
	vendorId = convertHexId(argv[3]);
	if(vendorId == -1)
	{
		fprintf(stderr, "Invalid vendor id '%s': expecting four-digit hexadecimal e.g. 0x045e\n", argv[3]);
		return -1;
	}
	
	// Allocate master IO port
	err = IOMasterPort(MACH_PORT_NULL, &masterPort);
	CHECKRETURN(err, "Failed to open master port");
	
	// Create matching dictionary
    matcher = IOServiceMatching(kIOUSBDeviceClassName);
    if(!matcher)
    {
		fprintf(stderr, "Failed to create matching dictionary\n");
		return -1;
    }
	
	// Create number references and add to dictionary
    numberRef = CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &productId);
    if(!numberRef)
    {
        fprintf(stderr, "Failed to create number reference for product ID\n");
        return -1;
    }
    CFDictionaryAddValue(matcher, CFSTR(kUSBProductID), numberRef);
    CFRelease(numberRef);
    numberRef = 0;
    numberRef = CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &vendorId);
    if(!numberRef)
    {
        fprintf(stderr, "Failed to create number reference for vendor ID\n");
        return -1;
    }
    CFDictionaryAddValue(matcher, CFSTR(kUSBVendorID), numberRef);
    CFRelease(numberRef);
    numberRef = 0;
	
	// Get matches from dictionary (this eats the dictionary)
    err = IOServiceGetMatchingServices(masterPort, matcher, &iterator);
	CHECKRETURN(err, "Failed to get matching servivces");
    matcher = 0;
    
	count = 0;
    while((usbService = IOIteratorNext(iterator)))
    {
		// Get plugin interface
		err = IOCreatePlugInInterfaceForService(
			usbService, kIOUSBDeviceUserClientTypeID, kIOCFPlugInInterfaceID, &pluginInterface, &score);
		CHECKRETURN(err, "Failed to create plugin interface for service");
		if(!pluginInterface)
		{
			fprintf(stderr, "Service did not return plugin interface\n");
			return -1;
		}
		
		// Now query for suitable USB device interface
		err = (*pluginInterface)->QueryInterface(pluginInterface, 
			CFUUIDGetUUIDBytes(kIOUSBDeviceInterfaceID245), (LPVOID)&deviceInterface);
		IODestroyPlugInInterface(pluginInterface);

		// Open device
		err = (*deviceInterface)->USBDeviceOpen(deviceInterface);
		CHECKRETURN(err, "Error opening device");
		
		// Suspend or resume device
		err = (*deviceInterface)->USBDeviceSuspend(deviceInterface, suspend);
		CHECKRETURN(err, "Error suspending or resuming device");
		
		// Close device
		err = (*deviceInterface)->USBDeviceClose(deviceInterface);
		CHECKRETURN(err, "Error closing device");
		err = (*deviceInterface)->Release(deviceInterface);
		CHECKRETURN(err, "Error releading device interface");
		
		// Release service
		IOObjectRelease(usbService);
		count++;
    }
	
	// Release iterator
    IOObjectRelease(iterator);
    iterator = 0;
	
	// Free master IO port
    mach_port_deallocate(mach_task_self(), masterPort);
	
	// Check count
	if(!count)
	{
		fprintf(stderr, "Device with product ID 0x%04x and vendor ID 0x%04x not found\n", productId, vendorId);
		return -1;
	}
	
    return 0;	
}
