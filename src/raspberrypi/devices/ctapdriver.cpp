//---------------------------------------------------------------------------
//
//	SCSI Target Emulator RaSCSI (*^..^*)
//	for Raspberry Pi
//
//	Powered by XM6 TypeG Technology.
//	Copyright (C) 2016-2020 GIMONS
//  Copyright (C) akuker
//
//	Imported NetBSD support and some optimisation patches by Rin Okuyama.
//
//	[ TAP Driver ]
//
//---------------------------------------------------------------------------

#include "os.h"
#include "xm6.h"
#include "ctapdriver.h"
#include "log.h"

//---------------------------------------------------------------------------
//
//	Constructor
//
//---------------------------------------------------------------------------
CTapDriver::CTapDriver()
{
	LOGTRACE("%s",__PRETTY_FUNCTION__);
	// Initialization
	m_hTAP = -1;
	memset(&m_MacAddr, 0, sizeof(m_MacAddr));
}

//---------------------------------------------------------------------------
//
//	Initialization
//
//---------------------------------------------------------------------------
#ifdef __linux__
BOOL FASTCALL CTapDriver::Init()
{
	LOGTRACE("%s",__PRETTY_FUNCTION__);

	char dev[IFNAMSIZ] = "ras0";
	struct ifreq ifr;
	int ret;

	ASSERT(this);

	// TAP device initilization
	if ((m_hTAP = open("/dev/net/tun", O_RDWR)) < 0) {
		LOGERROR("Error: can't open tun. Errno: %d %s", errno, strerror(errno));
		return FALSE;
	}

	// IFF_NO_PI for no extra packet information
	memset(&ifr, 0, sizeof(ifr));
	ifr.ifr_flags = IFF_TAP | IFF_NO_PI;
	strncpy(ifr.ifr_name, dev, IFNAMSIZ);
	if ((ret = ioctl(m_hTAP, TUNSETIFF, (void *)&ifr)) < 0) {
		LOGERROR("Error: can't ioctl TUNSETIFF. Errno: %d %s", errno, strerror(errno));
		close(m_hTAP);
		return FALSE;
	}

	// This doesn't work!!!!
	// if ((ret = ioctl(m_hTAP, SIOCGIFFLAGS, (void *)&ifr)) < 0) {
	// 	printf("Error: can't ioctl SIOCGIFFLAGS %s\n", strerror(errno));
	// 	close(m_hTAP);
	// 	return FALSE;
	// }

	// ifr.ifr_flags |= IFF_UP;
	// if ((ret = ioctl(m_hTAP, SIOCSIFFLAGS, (void *)&ifr)) < 0) {
	// 	printf("Error: can't ioctl SIOCSIFFLAGS %s\n", strerror(errno));
	// 	close(m_hTAP);
	// 	return FALSE;
	// }
	// The following is a temporarly way to force the tap interface up
	LOGDEBUG("ip link set ras0 up");
	system("ip link set ras0 up");
	LOGDEBUG("ifconfig ras0 192.168.0.1");
	system("ifconfig ras0 192.168.0.1");

	// Get MAC address
	ifr.ifr_addr.sa_family = AF_INET;
	if ((ret = ioctl(m_hTAP, SIOCGIFHWADDR, &ifr)) < 0) {
		LOGERROR("Error: can't ioctl SIOCGIFHWADDR. Errno: %d %s", errno, strerror(errno));
		close(m_hTAP);
		return FALSE;
	}

	// Save MAC address
	memcpy(m_MacAddr, ifr.ifr_hwaddr.sa_data, sizeof(m_MacAddr));
	LOGINFO("Tap device %s created", ifr.ifr_name);
	return TRUE;
}
#endif // __linux__

#ifdef __NetBSD__
BOOL FASTCALL CTapDriver::Init()
{
	struct ifreq ifr;
	struct ifaddrs *ifa, *a;
	
	ASSERT(this);

	// TAP Device Initialization
	if ((m_hTAP = open("/dev/tap", O_RDWR)) < 0) {
		LOGERROR("Error: can't open tap. Errno: %d %s", errno, strerror(errno));
		return FALSE;
	}

	// Get device name
	if (ioctl(m_hTAP, TAPGIFNAME, (void *)&ifr) < 0) {
		LOGERROR("Error: can't ioctl TAPGIFNAME. Errno: %d %s", errno, strerror(errno));
		close(m_hTAP);
		return FALSE;
	}

	// Get MAC address
	if (getifaddrs(&ifa) == -1) {
		LOGERROR("Error: can't getifaddrs. Errno: %d %s", errno, strerror(errno));
		close(m_hTAP);
		return FALSE;
	}
	for (a = ifa; a != NULL; a = a->ifa_next)
		if (strcmp(ifr.ifr_name, a->ifa_name) == 0 &&
			a->ifa_addr->sa_family == AF_LINK)
			break;
	if (a == NULL) {
		LOGERROR("Error: can't get MAC addressErrno: %d %s", errno, strerror(errno));
		close(m_hTAP);
		return FALSE;
	}

	// Save MAC address
	memcpy(m_MacAddr, LLADDR((struct sockaddr_dl *)a->ifa_addr),
		sizeof(m_MacAddr));
	freeifaddrs(ifa);

	LOGINFO("Tap device : %s\n", ifr.ifr_name);

	return TRUE;
}
#endif // __NetBSD__

//---------------------------------------------------------------------------
//
//	Cleanup
//
//---------------------------------------------------------------------------
void FASTCALL CTapDriver::Cleanup()
{
	ASSERT(this);

	// Release TAP defice
	if (m_hTAP != -1) {
		close(m_hTAP);
		m_hTAP = -1;
	}
}

//---------------------------------------------------------------------------
//
//	MGet MAC Address
//
//---------------------------------------------------------------------------
void FASTCALL CTapDriver::GetMacAddr(BYTE *mac)
{
	ASSERT(this);
	ASSERT(mac);

	memcpy(mac, m_MacAddr, sizeof(m_MacAddr));
}

//---------------------------------------------------------------------------
//
//	Receive
//
//---------------------------------------------------------------------------
int FASTCALL CTapDriver::Rx(BYTE *buf)
{
	struct pollfd fds;
	DWORD dwReceived;

	ASSERT(this);
	ASSERT(m_hTAP != -1);

	// Check if there is data that can be received
	fds.fd = m_hTAP;
	fds.events = POLLIN | POLLERR;
	fds.revents = 0;
	poll(&fds, 1, 0);
	if (!(fds.revents & POLLIN)) {
		return 0;
	}

	// Receive
	dwReceived = read(m_hTAP, buf, ETH_FRAME_LEN);
	if (dwReceived == (DWORD)-1) {
		return 0;
	}

	// If reception is enabled
	if (dwReceived > 0) {
		// Pad to the maximum frame size (60 bytes) excluding FCS
		if (dwReceived < 60) {
			memset(buf + dwReceived, 0, 60 - dwReceived);
			dwReceived = 60;
		}

		// Add a dummy FCS
		memset(buf + dwReceived, 0, 4);
		dwReceived += 4;
	}

	// Return the number of bytes
	return dwReceived;
}

//---------------------------------------------------------------------------
//
//	Send
//
//---------------------------------------------------------------------------
int FASTCALL CTapDriver::Tx(BYTE *buf, int len)
{
	ASSERT(this);
	ASSERT(m_hTAP != -1);

	// Start sending
	return write(m_hTAP, buf, len);
}
