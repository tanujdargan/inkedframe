// inkedframe: printed backboard (replaces the frame's 200 x 150 cardboard), with the Feather mount built in,
// keyholes for hanging landscape or portrait, a USB-C port block and a fold-out desk stand.
//
// Coordinates: the board's inside face is z=0 (towards the panel), the outside face z=-T. X and Y are as seen
// from the front of the frame (and so from the inside of the board): x right, y up, origin bottom-left.
// Portrait means the frame turned a quarter clockwise (as seen from the front): the right edge (x=200)
// becomes the bottom, the left edge the top.
//
// Parts (print in black PLA, 0.4 mm nozzle):
//   board_left, board_right  the two halves, outside face down on the bed, butted together at x = seam.
//   splice                   a flat strip over the seam on the inside: 6 x M3x6 from inside, nuts sunk in the
//                            outside face.
//   plunger_boot / plunger_reset   shorter plungers for the thinner stack (the old ones are too long)
//   port_block               holds the USB-C extension's female end at 30 deg over a slot; prints standing on
//                            its back end so the oval channel is clean. The channel is open along the top, so the
//                            female end and its cable drop in from above (a light click: the opening is 0.2 mm
//                            narrower than the plug). One zip tie round the cable behind the back wall stops it
//                            moving out (the back wall stops it moving in); a second goes through the slot under
//                            the channel and round the plug to hold it down.
//   stand_base, stand_leg    the leg snaps onto the base's pins; the base screws onto either socket (landscape
//                            or portrait) with 2 x M3x10. Take both off to hang the frame flat.
// Reused as printed: clamp (2 x M3x8 into nuts slid into the posts), battery tray (4 x M3x6 from inside).

use <mount.scad>   // feather(), clamp(), tray(), battery(), nut(), bolt() for the renders; clamp and tray reused

part = "inside";   // inside | outside | exploded | stand_side | section | <part names above>
$fn = 40;

// ---- measurements ---------------------------------------------------------------------------
B        = [200, 150];   // board, as supplied with the frame
T        = 3;            // board thickness
edge     = 5;            // keep inside features this far from the edges
frame_b  = 15;           // frame bottom outside edge to board bottom edge (for the leg length). MEASURE.
plug     = [14, 8, 45];  // extension's male plug at the Feather: width, thickness, length incl. cable bend
fem      = [12, 8, 24];  // female end: oval width, thickness, body length to where the cable starts. MEASURE length.
cable_d  = 4.5;          // extension cable diameter
drv_zone = [[62, 2], [128, 42]];   // display driver board footprint (reserved, nothing printed). CHECK.

// ---- Feather (positions from Adafruit's board file; same numbers as mount.scad) ----------------
pcb_t = 1.6; pcb_l = 50.8; pcb_w = 22.86;
g     = 3.8;               // board inside face to the Feather's component face (battery plug sits in a pocket)
z0    = g; z1 = g + pcb_t; // 3.8, 5.4
mz1   = 7.8;               // back face height in mount.scad, for reusing its models
fe_at = [112, 72];         // Feather USB-end corner; the board runs up (+y), USB end down (60+ mm of room below)
function fy(y) = pcb_w - y;
reset  = [10.795, fy(5.1435)];
boot   = [20.955, fy(5.207)];
neo    = [29.4005, fy(8.128)];
chg    = [1.4605, fy(5.461)];
hole_u = [[2.54, fy(20.32)], [2.54, fy(2.54)]];
hole_f = [[48.26, fy(20.955)], [48.26, fy(1.8415)]];
jst    = [10.922, fy(19.3675)];
post_d = 8.8; post_x = 1.6;
post_y = [-0.75 - post_d/2, pcb_w + 0.75 + post_d/2];
lip_x  = pcb_l + 0.8;
gboss  = 0.7;              // raised seat round each plunger guide (the STEMMA socket hangs down to 0.85)
poke_boot = 2; poke_reset = 1;
module at_feather() translate([fe_at[0], fe_at[1], 0]) rotate([0, 0, 90]) children();

// ---- layout ---------------------------------------------------------------------------------
seam      = 70;                      // where the halves butt together
splice_w  = 18;                      // strip over the seam on the inside
seam_y    = [20, 75, 130];
seam_dx   = [-4.5, 4.5];
module at_tray() translate([49.5, 41.6, 0]) rotate([0, 0, 90]) children();   // upright, mouth up
tray_boss = [[8, -5], [64.5 - 12, -5], [8, 36 + 5], [64.5 - 34, 36 + 5]];   // as in mount.scad's tray
port_at   = [165, 36];               // female end's front lower edge, opening facing down (-y); the plug comes in from below
stand_d   = 60;                      // hinge distance from the bottom edge, same in both orientations
sock_L    = [[100 - 14, stand_d], [100 + 14, stand_d]];   // landscape socket (hinge axis along x)
sock_P    = [[B[0] - stand_d, 75 - 14], [B[0] - stand_d, 75 + 14]];   // portrait socket (hinge axis along y)
// keyholes: landscape pair at the top corners (slot up); portrait pair down the left edge (slot left, towards
// the portrait top); the top-left one does both
keys      = [[[20, 122], [[0, 1], [-1, 0]]], [[180, 122], [[0, 1]]], [[20, 28], [[-1, 0]]]];

// ---- helpers ---------------------------------------------------------------------------------
module hex(f, h) cylinder(d = f / cos(30), h = h, $fn = 6);
module nut_flush() translate([0, 0, -T - 0.01]) hex(5.7, 2.6);          // nut sunk into the outside face
module rbox(s, r) hull() for (x = [r, s[0] - r], y = [r, s[1] - r]) translate([x, y, 0]) cylinder(r = r, h = s[2]);
INK = "#2b2b2b";

// ---- Feather mount (in Feather coordinates) -------------------------------------------------
module feather_add() {
  for (h = hole_u) translate([h[0], h[1], 0]) { cylinder(d = 3.8, h = z0); cylinder(d = 2.2, h = z1 + 1.0, $fn = 24); }
  for (h = hole_f) translate([h[0], h[1], 0]) cylinder(d = 3.0, h = z0);
  translate([lip_x, 3, 0]) cube([2.4, pcb_w - 6, z1 + 2.5]);
  translate([0, 3 + pcb_w - 6, 0]) rotate([90, 0, 0]) linear_extrude(pcb_w - 6)
    polygon([[lip_x, z1 - 0.5], [lip_x, z1 + 2.5], [lip_x - 2.3, z1 + 2.5], [lip_x - 2.3, z1 + 1.8]]);
  for (b = [reset, boot]) translate([b[0], b[1], 0]) cylinder(d = 7, h = gboss);
  for (y = post_y) translate([post_x, y, 0]) cylinder(d = post_d, h = z1 - 0.2);
}
module feather_cut() {
  translate([jst[0] - 4.6, -22, -2.4]) cube([9.2, 22 + jst[1] + 4.3, 20]);                 // battery plug pocket
  translate([-plug[2], 11.43 - plug[0]/2, (z0 - 1.6) - plug[1]/2 - 0.3]) cube([plug[2] + 0.5, plug[0], 20]); // USB plug trough
  for (b = [reset, boot]) translate([b[0], b[1], 0]) {
    translate([0, 0, -T - 1]) cylinder(d = 3.6, h = T + 5);
    translate([0, 0, gboss - 1.8]) cylinder(d1 = 3.6, d2 = 6.4, h = 1.4);
    translate([0, 0, gboss - 0.41]) cylinder(d = 6.4, h = 1);
  }
  translate([neo[0], neo[1], -T - 1]) cylinder(d = 2.2, h = 20);
  translate([chg[0], chg[1], -T - 1]) cylinder(d = 2.0, h = 20);
  for (i = [0, 1]) translate([post_x, post_y[i], 0]) {
    translate([0, 0, -2]) cylinder(d = 3.2, h = 20);
    translate([0, 0, 1.2]) rotate([0, 0, 30]) hex(5.7, 2.7);
    translate([-2.85, i == 0 ? -10 : 0, 1.2]) cube([5.7, 10, 2.7]);
  }
}

// ---- keyholes ---------------------------------------------------------------------------------
kh = [9, 4.6, 9];   // head hole, slot width, slot length
module keyhole_add(dirs)   // raised pad on the inside over the head pocket
  for (d = dirs) hull() { cylinder(d = kh[0] + 5, h = 2.4); translate(d * kh[2]) cylinder(d = kh[0] + 5, h = 2.4); }
module keyhole_cut(dirs) {
  translate([0, 0, -T - 1]) cylinder(d = kh[0], h = 10);
  for (d = dirs) {
    hull() for (p = [[0, 0], d * kh[2]]) translate([p[0], p[1], -T - 1]) cylinder(d = kh[1], h = 10);
    hull() for (p = [[0, 0], d * kh[2]]) translate([p[0], p[1], -T + 1.2]) cylinder(d = kh[0], h = T - 1.2 + 1.2);   // head pocket
  }
}

// ---- USB-C port block (local: female axis rises along +x at ua, opening faces -x) -------------
ua = 30; fW = fem[0] + 0.4; fT = fem[1] + 0.4; fL = fem[2];   // 0.2 mm clearance per side
port_lift = fL + 2.5;   // puts the back end on the bed
module oval(w, t, l) rotate([0, 90, 0]) hull() for (s = [-1, 1]) translate([0, s * (w - t)/2, 0]) cylinder(d = t, h = l);
module along_axis() rotate([0, -ua, 0]) translate([0, 0, fem[1]/2]) children();   // origin at the face centre line
pflange = [[6, -15], [6, 15]];       // port block screws (local x, y)
module port_block() {
  difference() {
    union() {
      hull() { along_axis() oval(fW + 4, fT + 4, fL + 2.5); translate([0, -(fW + 4)/2, 0]) cube([cos(ua) * (fL + 2.5), fW + 4, 0.01]); }
      hull() for (p = pflange) translate([p[0], p[1], 0]) cylinder(d = 9, h = 3);
      // webs from each flange back to the back wall, so the flanges don't start in mid-air when the block
      // prints standing on its back end
      for (p = pflange) hull() {
        translate([p[0], p[1], 0]) cylinder(d = 9, h = 3);
        translate([cos(ua) * (fL + 2.5) - 1, -1.5 + sign(p[1]) * 4, 0]) cube([1, 3, sin(ua) * (fL + 2.5) * 0.6]);
      }
    }
    translate([-50, -50, -50]) cube([100, 100, 50]);                                             // nothing below the board face
    along_axis() translate([-1, 0, 0]) oval(fW, fT, fL + 1);                                     // channel
    along_axis() translate([-1, -(fem[0] - 0.2)/2, 0]) cube([fL + 1, fem[0] - 0.2, 20]);           // open along the top
    along_axis() translate([fL * 0.55 - 2.5, -20, -fT/2 - 4.6]) cube([5, 40, 2.4]);                 // zip-tie slot under the channel
    along_axis() translate([fL - 1, -(cable_d + 0.4)/2, -cable_d/2 - 0.2]) cube([10, cable_d + 0.4, 20]);  // cable slot, open on top
    for (p = pflange) translate([p[0], p[1], -1]) cylinder(d = 3.4, h = 10);
  }
}
module female_model() color("#d8d8dc") along_axis() oval(fem[0], fem[1], fem[2]);
module port_slot() {   // through the board for the charging plug
  vmin = fem[1]/2 - 4.25; vmax = fem[1]/2 + 4.25;
  x0 = -T / tan(ua) - vmax / sin(ua) - 0.5; x1 = -vmin / sin(ua) + 0.5;
  translate([x0, -7.25, -T - 1]) cube([x1 - x0, 14.5, T + 2]);
}
module at_port() translate([port_at[0], port_at[1], 0]) rotate([0, 0, 90]) children();   // local +x -> board +y

// ---- stand --------------------------------------------
// Base, leg and hinge pin print separately, flat. The leg's three barrels sit between the base's two, and the
// pin goes through all five. Hinge frame: axis along x at (y = 0, z = sR); z is away from the board; the leg
// folds down (-y) flat against the board and opens to one angle, where tails on its barrels press on the board.
// The base's two screw holes sit 10 mm above the hinge and line up with the board's sockets (2 x M3x10).
sR = 5;  stp = 4;  sgap = 0.4;           // barrel radius, plate thickness, print-in-place clearance
sW = 20;                                // half width of the leg at the hinge
s_scr = 10;                             // screw line above the hinge axis
h_hinge = frame_b + stand_d - s_scr;    // hinge height above the table, along the frame
s_tilts = [12, 18, 24];
alpha = s_tilts[1];                     // lean angle when open (beta 50)
leg_L = (h_hinge * cos(s_tilts[1]) - sR * sin(s_tilts[1])) / cos(50 - s_tilts[1]);   // middle click at beta 50
kn_leg  = [[-sW, -14.8], [-4, 4], [14.8, sW]];    // leg barrels (x)
kn_base = [[-14.4, -4.4], [4.4, 14.4]];           // base barrels (x)

module yz(x0, x1) translate([x0, 0, 0]) rotate([90, 0, 90]) linear_extrude(x1 - x0) children();   // 2D (y, z) -> 3D
module barrel(k, r = sR) yz(k[0], k[1]) translate([0, sR]) circle(r = r, $fn = 64);

// Separate parts, all printed flat: base, leg, and a printed hinge pin (head on one end, snug in the last barrel).
// The leg opens to beta 50 (about 18 deg of lean): stop tails on all three leg barrels press on the board.
pin_d = 4.5;
s_tail = (sR - (stp / 2) * cos(50)) / sin(50);     // tail length that meets the board at beta 50
module pin_hole(k, d) translate([k[0] - 0.01, 0, sR]) rotate([0, 90, 0]) cylinder(d = d, h = k[1] - k[0] + 0.02, $fn = 40);
module stand_leg() difference() {   // folded position
  union() {
    for (k = kn_leg) hull() { barrel(k); translate([k[0], -sR - sgap - 0.6, 0]) cube([k[1] - k[0], 0.6, stp]); }
    for (k = kn_leg) translate([k[0], 0, sR - stp / 2]) cube([k[1] - k[0], s_tail, stp]);                        // stop tails
    hull() { translate([-sW, -sR - sgap - 0.6, 0]) cube([2 * sW, 0.6, stp]); translate([-12, -leg_L, 0]) cube([24, 3, stp]); }
  }
  for (k = kn_leg) pin_hole(k, k == kn_leg[2] ? pin_d : pin_d + 0.3);    // snug in the last barrel, free in the others
}
module stand_base() difference() {
  union() {
    translate([-sW + sgap, sR + sgap, 0]) cube([2 * (sW - sgap), 22 - sR - sgap, stp]);
    for (k = kn_base) hull() { barrel(k); translate([k[0], sR + sgap, 0]) cube([k[1] - k[0], 0.6, stp]); }
  }
  for (k = kn_base) pin_hole(k, pin_d + 0.3);
  for (sx = [-1, 1]) translate([sx * 14, s_scr, -1]) cylinder(d = 3.4, h = 20);
}
module stand_pin() { cylinder(d = 8, h = 1.6, $fn = 40); cylinder(d = pin_d, h = 1.6 + 2 * sW, $fn = 40); }   // prints standing on its head
module pin_model() color("#e9e6dd") translate([-sW - 1.6, 0, sR]) rotate([0, 90, 0]) stand_pin();
module stand_print() { stand_base(); stand_leg(); }
module leg_at(b) translate([0, 0, sR]) rotate([-b, 0, 0]) translate([0, 0, -sR]) stand_leg();
module leg_open() leg_at(50);
module leg_stored() stand_leg();
module axle_model() pin_model();
// on the board: base screws to the socket, outside face, leg pointing "down" for that orientation
module stand_at(sock, rot, open = true) {
  c = (sock[0] + sock[1]) / 2;
  translate([c[0], c[1], -T]) rotate([0, 0, rot]) mirror([0, 0, 1]) translate([0, -s_scr, 0]) {
    color(INK) stand_base();
    axle_model();
    color("#3a3a3a") if (open) leg_open(); else leg_stored();
  }
}

// ---- the board ----------------------------------------------------------------------------------
module board_whole() difference() {
  union() {
    translate([0, 0, -T]) cube([B[0], B[1], T]);
    at_feather() feather_add();
    for (k = keys) translate([k[0][0], k[0][1], 0]) keyhole_add(k[1]);
    for (s = concat(sock_L, sock_P)) translate([s[0], s[1], 0]) cylinder(d = 10, h = 5);
    at_tray() for (b = tray_boss) translate([b[0], b[1], 0]) cylinder(d = 9, h = 0.6);
  }
  at_feather() feather_cut();
  for (k = keys) translate([k[0][0], k[0][1], 0]) keyhole_cut(k[1]);
  for (s = concat(sock_L, sock_P)) translate([s[0], s[1], 0]) {
    translate([0, 0, -T - 1]) cylinder(d = 3.4, h = 20);
    translate([0, 0, 1.2]) rotate([0, 0, 30]) hex(5.7, 2.7);
    translate([-2.85, 0, 1.2]) cube([5.7, 10, 2.7]);
  }
  at_tray() for (b = tray_boss) translate([b[0], b[1], 0]) { translate([0, 0, -T - 1]) cylinder(d = 3.4, h = 10); nut_flush(); }
  at_port() { port_slot(); for (p = pflange) translate([p[0], p[1], 0]) { translate([0, 0, -T - 1]) cylinder(d = 3.4, h = 10); nut_flush(); } }
  for (y = seam_y, dx = seam_dx) translate([seam + dx, y, 0]) { translate([0, 0, -T - 1]) cylinder(d = 3.4, h = 10); nut_flush(); }
}
module splice() difference() {
  translate([seam - splice_w/2, edge, 0]) rbox([splice_w, B[1] - 2 * edge, 3], 2);
  for (y = seam_y, dx = seam_dx) translate([seam + dx, y, -1]) cylinder(d = 3.4, h = 10);
}
module board_left()  intersection() { board_whole(); translate([-1, -1, -T - 1]) cube([seam + 1, B[1] + 2, 40]); }
module board_right() intersection() { board_whole(); translate([seam, -1, -T - 1]) cube([B[0], B[1] + 2, 40]); }

// ---- plungers (shorter: the board is the guide) --------------------------------------------------
module plunger3(poke) {
  tip = -T - poke; stem = z0 - 1.9 - 0.3;
  translate([0, 0, tip]) hull() { cylinder(d = 2.6, h = 0.01); translate([0, 0, 0.4]) cylinder(d = 3.2, h = 0.01); }
  translate([0, 0, tip + 0.4]) cylinder(d = 3.2, h = gboss - 1.8 - tip - 0.4 + 0.01);
  translate([0, 0, gboss - 1.8]) cylinder(d1 = 3.2, d2 = 6.0, h = 1.4);
  translate([0, 0, gboss - 0.4]) cylinder(d = 6.0, h = 0.4);
  translate([0, 0, gboss]) cylinder(d = 2.6, h = stem - gboss);
}

// ---- scene ----------------------------------------------------------------------------------------
module electronics(e = 0) {
  at_feather() {
    for (b = [[reset, poke_reset, "#3d7be0"], [boot, poke_boot, "#e0603d"]]) translate([b[0][0], b[0][1], 8 * e]) color(b[2]) plunger3(b[1]);
    translate([0, 0, z1 - mz1 + 22 * e]) feather();
    translate([0, 0, z1 - mz1 + 34 * e]) color(INK) clamp();
  }
  at_tray() translate([0, 0, 0.6 + 14 * e]) { color(INK) tray(); battery(0); }
  at_port() translate([0, 0, 14 * e]) { color(INK) port_block(); female_model(); }
  color("#4a7bd0", 0.25) translate([drv_zone[0][0], drv_zone[0][1], 12]) cube([drv_zone[1][0] - drv_zone[0][0], drv_zone[1][1] - drv_zone[0][1], 1.6]);
}
// the halves are drawn from their exported STLs (stl/board_*.stl): OpenSCAD's preview mis-draws the CSG
module boards(e = 0) {
  translate([0, 0, 10 * e]) color("#555") splice();
  color("#3b3b3b") translate([0, 0, -T]) import("stl/board_left.stl");
  translate([0, 0, 6 * e]) color("#454545") translate([0, 0, -T]) import("stl/board_right.stl");
}

if (part == "inside") { boards(); electronics(); }
else if (part == "exploded") { boards(1); electronics(1); }
else if (part == "outside") { boards(); stand_at(sock_L, 0, true); }
else if (part == "outside_stored") { boards(); stand_at(sock_L, 0, false); }
else if (part == "outside_portrait") { boards(); stand_at(sock_P, 90, true); }
else if (part == "stand_side" || part == "stand_side_p") {   // on a table, seen from the side
  P = part == "stand_side_p";
  // board coords -> world: board "up" for this orientation to world z, the board's front (+z) to world -y,
  // then tilt back by alpha and stand the frame's bottom edge on the table
  bottom = P ? 0 : 0;
  translate([0, 0, (frame_b + (P ? B[0] : 0)) * cos(alpha)]) rotate([-alpha, 0, 0]) rotate([90, 0, 0]) rotate([0, 0, P ? -90 : 0]) {
    color("#8a6a4a", 0.35) translate([-frame_b, -frame_b, -T]) cube([B[0] + 2 * frame_b, B[1] + 2 * frame_b, 30 + T]);
    boards(); electronics();
    if (P) stand_at(sock_P, 90, true); else stand_at(sock_L, 0, true);
  }
  color("#cfcfcf") translate([-120, -150, -2]) cube([400, 300, 2]);
}
else if (part == "board_left") translate([0, 0, T]) board_left();     // outside face on the bed
else if (part == "board_right") translate([0, 0, T]) board_right();
else if (part == "plunger_boot") translate([0, 0, T + poke_boot]) plunger3(poke_boot);
else if (part == "plunger_reset") translate([0, 0, T + poke_reset]) plunger3(poke_reset);
else if (part == "port_block") port_block();
else if (part == "splice") splice();
else if (part == "port_block_print") translate([0, 0, port_lift]) rotate([0, 120, 0]) port_block();   // back end on the bed, tunnel upright
else if (part == "stand_pin") stand_pin();
else if (part == "stand_base") stand_base();
else if (part == "stand_leg") stand_leg();
