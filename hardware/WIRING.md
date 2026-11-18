# XL adapter for the Info Orbs: soldering guide (2026-11-15)

Drawings (in `renders/`): `wiring_1_top.png` (top side: strips + XL), `wiring_2_under.png` (underside: every link and
wire), `wiring_3_side.png` (which way the strips go in), `wiring_4_usb.png` (the case USB board). Made by
`draw_wiring.py`. Board facts: the orbs board's v0.2 design file (brettdottech/info-orbs b6ead4e), photos
and meter checks 17-19, caliper readings 1-24 (BOARD_FACTS.md).

## Names used in every step
- **the XL**: the Pimoroni Pico LiPo 2 XL W.
- **the adapter board**: the piece of perfboard, 10 holes wide x 27 holes long.
- **column c0-c9, row r0-r26**: holes on the adapter board, seen from the TOP; c0 = left, r0 = the case end.
- **the socket strips**: two 19-pin header strips (columns c0 and c9, rows r0-r18). They plug into the orbs' socket.
- **the XL strips**: two 27-pin header strips (columns c1 and c8, rows r0-r26). The XL sits on them.
- **the case USB board**: the green "V1297" board in the orbs with the USB-C port.
- **the cut cable**: the cut USB-C cable (green = D+, white = D-).
- **pin names**: socket pins use the ESP32's printed names (3V3 ... 5V on c0, GND ... CLK on c9). XL pins use the
  XL's own print ("VB", "34", "-" ...).

## Parts and tools
Perfboard; male header strips (cut two 27s and two 19s from 40-pin strips); solid-core hookup wire 22-24 AWG: red
30 mm, black 40 mm, one other colour 55 mm, plus bare offcuts (cut resistor leads work) for six 5 mm links; Kapton
tape (optional); the cut cable; soldering iron, flush cutters, pliers, a multimeter, a marker.

## Pin map (for the firmware later)
SPI0 clock GP34, data GP7; DC GP15; reset GP14; chip selects: D1 GP22, D2 GP16, D3 GP46, D4 GP17, D5 GP31;
buttons (5 V when pressed, chip pull-downs on): LFT GP18, OK GP19, RHT GP20; USB 5 V into VB.

## Steps
1. Cut the adapter board to 10 holes x 27 holes. Put a marker dot beside the corner hole c0 r0 on BOTH sides.
2. Cut two 19-pin and two 27-pin strips. On one 27-pin strip (the c1 strip) pull out the 7th pin from one end. On the
   other (the c8 strip) pull out the 1st, 7th and 11th pins from one end. That end goes at row r0.
3. Socket strips: from the top, into c0 r0-r18 and c9 r0-r18. Plastic on top, long ends down (4.4 mm below the
   board). Turn the board over, resting it on the plastic, and solder all 38 on the underside. Cut the short ends
   on top off flush with the plastic.
4. XL strips: from the top, into c1 r0-r26 and c8 r0-r26 (the pulled pins at r6 on c1; r0, r6, r10 on c8). Plastic
   on top, long ends down. Optional: a strip of Kapton on the top side across rows r0-r3 between the two XL strips.
5. Put the XL on the XL strips: print facing up, its USB-C at row r26 (the end where the socket strips stop short),
   its "VB" corner over column c1. Solder its 50 pins on the XL's top. Leave its 4 holes over the pulled pins empty.
   Turn over and solder the XL strips' 50 pins on the underside.
6. Meter check before any wire (dial 200 ohm): one probe on the XL's USB-C metal shell, the other on the long pin at
   c8 r4: should show about 0. On c1 r26: should show 1. If c8 r4 shows 1, stop: something is wrong; find it before any wire.
7. Underside links and wires (drawing 2), every end soldered INTO the pin's joint at the board, nothing through a hole:
   - Bare 5 mm links to the pin right beside it: R1 c9 r1 (P23) to c8 r1 (XL 34); R2 c9 r5 (P21) to c8 r5 (XL 31);
     R3 c9 r7 (P19) to c8 r7 (XL 15); R4 c9 r8 (P18) to c8 r8 (XL 14); L1 c0 r7 (P33) to c1 r7 (XL 16);
     L2 c0 r8 (P25) to c1 r8 (XL 17).
   - Short insulated links (8 mm piece, 2 mm bare each end) to the pin one row further: L4 c0 r14 (P13) to c1 r15
     (XL 22); L5 c0 r9 (P26) to c1 r10 (XL 18); L6 c0 r10 (P27) to c1 r11 (XL 19); L7 c0 r11 (P14) to c1 r12 (XL 20).
   - Wires (2 mm bare each end, lying flat along the route):
     L3, 20 mm: c0 r6 (P32) across the empty c1 r6 to c2 r6, along column c2 to r3, into c1 r3 (XL $46).
     L8, red, 27 mm: c0 r18 (5V) along column c0 (empty holes r19-r26) to r26, into c1 r26 (XL VB).
     R5, 30 mm: c9 r10 (P17) across the empty c8 r10 to c7 r10, along column c7 to r17, into c8 r17 (XL 7).
     R6, black, 22 mm: c9 r0 (GND) across the empty c8 r0 to c7 r0, along column c7 to r4, into c8 r4 (XL -).
     R7, black, 17 mm: c9 r6 (GND) across the empty c8 r6 to c7 r6, along column c7 to r4, into c8 r4 (XL -).
8. Meter checks (200 ohm): each pair should show about 0, probes on the socket pin's long end and the XL pin's top:
   P23-XL 34, P21-31, P19-15, P18-14, P17-7, GND(c9 r0)-XL "-" between 31 and 32, GND(c9 r6)-same, P33-16, P25-17,
   P32-$46, P13-22, P26-18, P27-19, P14-20, 5V-VB. Each of these should show 1: every bold socket pin against the
   socket pin above and below it in its strip; 5V against GND (c9 r0); GND (c9 r6) against the XL's "+".
9. Powered check, adapter on the table: USB cable from the rig into the XL's USB-C. Meter dial 20 V DC: red probe on
   the 5V pin's long end, black on GND (c9 r0): about 5. Check the PC sees the XL as a USB device. Unplug.
10. The cut cable (orbs unplugged, back side up): green and white cut to 35 mm from the moulding, 2 mm stripped and
    tinned; every other wire and any braid cut back to 3 mm and covered with tape or heat-shrink. Twist green and white
    3 turns. Solder green onto the tip of the case USB board's middle pin next to GND, white onto the middle pin next
    to VCC (drawing 4). Meter: green joint to the GND end pin 1; white joint to the VCC end pin 1; green to white 1.
11. Fit it: ESP32 out. Adapter in with the XL's USB-C pointing the same way as the board's "!USB This Way!" print
    (toward the case USB board), both 19-pin rows fully inside the socket. Plug the cut cable into the XL's USB-C.
12. First power-up (no sound on the orbs): the orbs' usual USB-A-to-C cable into a PC. Check the PC
    sees the XL through the case port. Screens may light blank: normal, there's no screen program yet.
    Unplug, close with the new base and the 20 mm screws.

## Changed from the layout sketch
- Buttons: straight links, no 10k/15k resistors (the XL's chip is A4: GPIO 18-20 take 5 V while it's powered).
- Routes go through 4 holes left empty by pulling XL strip pins (BT, 35, +, 13), so no wire crosses a joint.
- SDA reaches XL 7 along column c7; the 5 V wire runs along column c0 below the socket strips.
