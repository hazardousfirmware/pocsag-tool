# pocsag-tool
Implementation of POCSAG pager protocol and message encoder for SDR

## TOOLS:

**pocsag** - encodes message in POCSAG protocol for tranmission by Software Defined Dadio,
      generates binary file with preamble, sync codewords, encoded data and CRC (BCH code)

**bin2audio** - modulates binary data into a NRZ line code, generates an audio file at the specified BAUD rate.
Can be used for other Software Defined Radio protocols, not just POCSAG
