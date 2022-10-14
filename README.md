# pocsag-tool
Implementation of POCSAG pager protocol and message encoder for SDR

## TOOLS:

**pocsag** - encodes message in POCSAG protocol for tranmission by Software Defined Dadio,
      generates binary file with preamble, sync codewords, encoded data and CRC (BCH code)

**bin2audio** - modulates binary data into a NRZ line code, generates an audio file at the specified BAUD rate.
Can be used for other Software Defined Radio protocols, not just POCSAG

**nb_fsk.grc** - GNU Radio flow graph to modulate the line code as narrow-band FM and broadcast


## Example usage
```sh
./pocsag -a 4444 -m "Broadcast this on hackrf" -f - | ./bin2audio -i - -o example.wav

cat example.wav | sox -t raw -esigned-integer -b16 -r 48000 - -esigned-integer -b16 -r 22050 -t raw - | multimon-ng -t raw  -a POCSAG1200 -
```

## Legal
This software contains code that transmits radio signals. POCSAG / pager messages are used by emergency services, Do not interfere with them. Do not broadcast on illegal frequencies.

For educational purpose only.
