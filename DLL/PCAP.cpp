
#include "stdafx.h"
#include "PCAP.h"

vector<PCAPFile*> PCAP::s_vPCAPFiles;
unsigned int PCAP::s_nPacketSize = 65535;

// Set maximum packet size

void PCAP::SetPacketSize(unsigned int p_nPacketSize)
{
	s_nPacketSize = p_nPacketSize + PACKET_HEADER_SIZE;
}

// Create a PCAP struct

PCAPFile* PCAP::CreatePCAP(string p_sFilepatn)
{
	PCAPFile* pcap = new PCAPFile();
	pcap->sFilename = p_sFilepatn;

	pcap->bHeaderWritten = false;
	InitializeCriticalSection(&pcap->oCriticalSection);

	srand((unsigned int)time(NULL));
	pcap->nAck = (uint32_t)rand();
	pcap->nSeq = (uint32_t)rand();

	s_vPCAPFiles.push_back(pcap);

	return pcap;
}

// Find a PCAP struct by filepath or create it

PCAPFile* PCAP::GetPCAP(string p_sFilename)
{
	for (size_t i = 0; i < s_vPCAPFiles.size(); i++)
	{
		if (p_sFilename.compare(s_vPCAPFiles[i]->sFilename) == 0) return s_vPCAPFiles[i];
	}

	// Or create it

	return CreatePCAP(p_sFilename);
}

// Write the PCAP file

void PCAP::WriteHeader(PCAPFile *p_pPCAP)
{
	pcap_hdr_s header;

	// Build struct

	header.magic_number = 0xA1B2C3D4;
	header.version_major = 2;
	header.version_minor = 4;
	header.thiszone = 0;
	header.sigfigs = 0;
	header.snaplen = s_nPacketSize;
	header.network = LINKTYPE_IPV4;

	// Write the header

	p_pPCAP->bHeaderWritten = true;
	Utils::WriteToTempFile(p_pPCAP->sFilename, (unsigned char *)&header, sizeof(header));
}

// Create packet header

pcaprec_hdr_s PCAP::CreatePacketHeader(size_t nLength)
{
	pcaprec_hdr_s header;
	SYSTEMTIME t;
	GetSystemTime(&t);

	// Create header

	header.ts_sec = (uint32_t)time(NULL);
	header.ts_usec = (uint32_t)(t.wMilliseconds * 1000);
	header.incl_len = (uint32_t)(nLength + 40);
	header.orig_len = (uint32_t)(nLength + 40);

	return header;
}

// Create packet contents, including TCP/IP header (https://github.com/google/ssl_logger/blob/master/ssl_logger.py)

unsigned char* PCAP::CreatePacket(PCAPFile *p_pPCAP, unsigned char *p_pcData, size_t p_nSize,
	bool p_bDataSent, string p_sSrcIP, string p_sDstIP, size_t p_nSrcPort, size_t p_nDstPort)
{
	ip_header_t  ipHeader;
	tcp_header_t tcpHeader;
	unsigned char *pData = NULL;
	uint32_t seq = 0, ack = 0;

	// Get SEQ and ACK

	if (p_bDataSent)
	{
		seq = p_pPCAP->nSeq;
		ack = p_pPCAP->nAck;
	}
	else
	{
		seq = p_pPCAP->nAck;
		ack = p_pPCAP->nSeq;
	}

	// Set up IP header

	ipHeader.ver_ihl = 0x45;
	ipHeader.tos = 0;
	ipHeader.total_length = HTONS((uint16_t)(p_nSize + PACKET_HEADER_SIZE));
	ipHeader.id = 0;
	ipHeader.flags_fo = HTONS(0x4000);
	ipHeader.ttl = 0xFF;
	ipHeader.protocol = 6;
	ipHeader.checksum = 0;

	// IP addresses 

	if (p_bDataSent)
	{
		ipHeader.src_addr = 0x31313131;
		ipHeader.dst_addr = 0x32323232;
	}
	else
	{
		ipHeader.src_addr = 0x32323232;
		ipHeader.dst_addr = 0x31313131;
	}

	// Set up TCP header

	tcpHeader.src_port = HTONS((uint16_t)p_nSrcPort);
	tcpHeader.dst_port = HTONS((uint16_t)p_nDstPort);
	tcpHeader.seq = HTONL(seq);
	tcpHeader.ack = HTONL(ack);
	tcpHeader.len_and_flags = HTONS(0x5018);
	tcpHeader.window_size = 0xFFFF;
	tcpHeader.checksum = 0;
	tcpHeader.urgent_p = 0;

	// Update SEQ and ACK

	if (p_bDataSent) p_pPCAP->nSeq += (uint32_t)p_nSize;
	else p_pPCAP->nAck += (uint32_t)p_nSize;

	// Create packet

	pData = new unsigned char[sizeof(ipHeader) + sizeof(tcpHeader) + (uint16_t)p_nSize];
	memcpy(pData, (void *)&ipHeader, sizeof(ipHeader));
	memcpy(pData + sizeof(ipHeader), (void *)&tcpHeader, sizeof(tcpHeader));
	memcpy(pData + sizeof(ipHeader) + sizeof(tcpHeader), (void *)p_pcData, (uint16_t)p_nSize);

	return pData;
}

// Write data to PCAP file

void PCAP::WriteData(string p_sFilename, unsigned char *p_pcData, size_t p_nSize, bool p_bDataSent,
	string p_sSrcIP, string p_sDstIP, size_t p_nSrcPort, size_t p_nDstPort)
{
	pcaprec_hdr_s pheader = CreatePacketHeader(p_nSize);
	PCAPFile *pcap = GetPCAP(p_sFilename);
	EnterCriticalSection(&pcap->oCriticalSection);

	// Write pcap header (if not written) and packet header

	if (pcap->bHeaderWritten == false) WriteHeader(pcap);
	Utils::WriteToTempFile(p_sFilename, (unsigned char *)&pheader, sizeof(pheader));

	// Write packet data

	unsigned char *pData = CreatePacket(pcap, p_pcData, p_nSize, p_bDataSent, p_sSrcIP, p_sDstIP, p_nSrcPort, p_nDstPort);
	Utils::WriteToTempFile(p_sFilename, pData, sizeof(ip_header_t) + sizeof(tcp_header_t) + (uint16_t)p_nSize);
	delete[] pData;

	LeaveCriticalSection(&pcap->oCriticalSection);
}
