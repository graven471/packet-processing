#include "parser.h"
#include <endian.h>
#include <stdio.h>

// definition:
// RTP (Real-time Transport Protocol) is a protocol used to deliver audio and video over IP networks.
//  It doesn't guarantee delivery or timing it just packages the data so it can be played back correctly at the receiver.

bool rtp_parse(const uint8_t *_Nonnull rtp_buf, size_t len)

{
	// [RTP header 12 bytes] [RTP payload]
	/*
	   0                   1                   2                   3
       0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
      |V=2|P|X|  CC   |M|     PT      |       sequence number         |
      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
      |                           timestamp                           |
      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
      |           synchronization source (SSRC) identifier            |
      +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
      |            contributing source (CSRC) identifiers             |
      |                             ....                              |
      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
      RTP header according to RFC 3550
      V = 2 means version 2

      Marker bit (M) : 1 bit
          Set for the very last packet of the access unit indicated by the
          RTP timestamp Decoders MAY use this bit as an early indication of the last packet of an access unit but MUST
          NOT rely on this property.

      Payload type (PT): 7 bits
           96 for H264

      Sequence number (SN): 16 bits
          For the single NALU and non-interleaved packetization mode, the sequence number is used to
          determine decoding order for the NALU.

      Timestamp: 32 bits
            The RTP timestamp is set to the sampling timestamp of the content.
            A 90 kHz clock rate MUST be used.
	 */

	// todo: handle error

	if (len < RTP_HDR_LEN)
		return false;

	const uint8_t *rtp_hdr = rtp_buf;

	if (((rtp_hdr[0] >> 6) & 0x03) != 2)
		return false;

	uint8_t p = (rtp_hdr[0] >> 5) & 0x01;
	uint8_t x = (rtp_hdr[0] >> 4) & 0x01;
	uint8_t cc = rtp_hdr[0] & 0x0F;

	uint8_t m = (rtp_hdr[1] >> 7) & 0x01;
	uint8_t pt = rtp_hdr[1] & 0x7F;

	printf("PT = (%u)\n", pt);

	// 96 = H264
	// (todo): do not do this let this parser be generic and handle this into H264 code
	if (pt != RTP_PT_H264)
		return false;

	uint16_t seq_num = be16toh(*(uint16_t *)(rtp_hdr + 2));
	uint32_t timestamp = be32toh(*(uint32_t *)(rtp_hdr + 4));
	uint32_t ssrc = be32toh(*(uint32_t *)(rtp_hdr + 8));

	uint32_t csrc_size = cc * 4;

	// nal starts from RTP_HDR_LEN + csrc_size
	if (len < RTP_HDR_LEN + csrc_size)
		return false;

	// (fixme): if x = 1 then there is a extension length i have to skip that
	const uint8_t *rtp_payload = &rtp_buf[RTP_HDR_LEN + csrc_size];

	/*
	 0                   1                   2                   3
     0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
    |F|NRI|  Type   |                                               |
    +-+-+-+-+-+-+-+-+                                               |
    |                                                               |
    |               Bytes 2..n of a single NAL unit                 |
    |                                                               |
    |                               +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
    |                               :...OPTIONAL RTP padding        |
    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
    Figure 2.  RTP payload format for single NAL unit packet
    RFC 6184 section 5.6
   */

	const uint8_t nal = rtp_payload[0];
	// this is forbidden bit must be 0
	if (((nal >> 7) & 0x01) != 0)
		return false;

	// nal_ref_idc tells how important nal is for reference
	// the NRI value of non-reference pictures is 00 as mandated by H.264.
	const uint8_t nri = (nal >> 5) & 0x3;

	// (rfc): The single NAL unit mode is targeted for conversational systems that comply with ITU-T Recommendation H.241
	// so nal_unit_type from 1-23 is "single NAL unit mode"
	// 24 — STAP-A
	// 28 — FU-A
	const uint8_t nal_unit_type = nal & 0x1F;

	return true;
}
