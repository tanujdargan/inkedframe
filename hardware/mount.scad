// inkedframe: the parts inside the frame, on the backboard. Print in black PLA, 0.4 mm nozzle.
//
//   feather_plate  holds the Feather ESP32-S3 face down (buttons towards the backboard), as low as the
//                  battery connector allows (its back face is 7.8 mm off the backboard). The USB end sits
//                  on two pins and is held by the clamp bar, screwed into two 6 mm male-female standoffs
//                  that also bolt the plate to the backboard (nut outside). The other end slides under a
//                  printed lip. Holes carry the NeoPixel and charge LED light out through the backboard.
//   clamp          the bar over the Feather's USB end. Print top face down.
//   plunger_boot / plunger_reset
//                  button plungers. Boot stands 2 mm proud of the backboard, Reset 1 mm, so Reset is hard
//                  to press by accident. Print standing on the tip.
//   tray           the battery tray: open top, rails along the long sides, a flexing side wall with a bump
//                  that clicks behind the battery. Slide the battery in or out by its body.
//   usb_cradle / usb_strap
//                  hold the female end of the USB-C extension at 30 degrees over a slot in the backboard,
//                  so a cable can be plugged in from outside. A straight-through mount would not fit the depth.
//
// Feather positions come from Adafruit's board file (Adafruit-Feather-ESP32-S3-PCB, .brd).
// Coordinates: X along the Feather (USB end at X=0), Y across it, Z away from the backboard; the
// backboard's inside face is Z=0.

part = "assembly";   // assembly | exploded | section | outside | <a part name above>
$fn = 48;

// ---- measurements --------------------------------------------------------------------------
board_t   = 2.0;    // backboard
plate_t   = 3.0;    // feather plate; also the plunger guide
gap       = 3.2;    // plate top to the Feather's component face (USB-C shell is 3.2 tall)
jst_h     = 6.0;    // battery connector height: the plate is cut away under it
pcb_t     = 1.6;
pcb_l     = 50.8;
pcb_w     = 22.86;
poke_boot  = 2.0;   // how far each plunger stands proud of the backboard's outside face
poke_reset = 1.0;

bat       = [60, 35, 8];   // battery L x W x thickness; the wire leaves near a corner of a 35 mm edge
wall      = 1.6;

usbf      = [12, 8, 28];   // USB-C extension female end: oval 12 wide x 8 thick; length only sets how far back it sticks out
usb_plug  = [13.5, 8.5];   // the charging cable's plug that goes in from outside (for the backboard slot)
usb_angle = 30;

// M3 (nylon kit)
m3_clear  = 3.4;
m3_nut_f  = 5.7;    // 5.5 across flats + clearance
m3_nut_h  = 2.6;
so_f      = 5.7;    // standoff hex across flats + clearance (5.5 like the nuts; a smaller one is still held by its screws)
so_len    = 6.0;    // male-female standoff body

// ---- derived --------------------------------------------------------------------------------
pcb_z0  = plate_t + gap;       // component face: 6.2
pcb_z1  = pcb_z0 + pcb_t;      // back face: 7.8
so_floor = pcb_z1 - 0.2 - so_len;   // standoff seat; the clamp pulls down 0.2 onto the board
clamp_t = 3.0;

function fy(y) = pcb_w - y;     // board is face down: plate Y = 22.86 - board y
reset   = [10.795, fy(5.1435)];
boot    = [20.955, fy(5.207)];
neo     = [29.4005, fy(8.128)];
chg     = [1.4605, fy(5.461)];
hole_u  = [[2.54, fy(20.32)], [2.54, fy(2.54)]];        // 2.5 mm plated, USB end
hole_f  = [[48.26, fy(20.955)], [48.26, fy(1.8415)]];   // 2.2 mm, antenna end
jst     = [10.922, fy(19.3675)];
module_x0 = 30.39;                                       // ESP32-S3-MINI-1 starts here, hangs 2.4 below the board

post_d  = 8.8;
post_x  = 1.6;
post_y  = [-0.75 - post_d/2, pcb_w + 0.75 + post_d/2];
fboss   = [[44, -5.5], [44, pcb_w + 5.5]];
boss_d  = 9;
boss_h  = 5;
lip_x   = pcb_l + 0.8;          // inside face of the antenna-end wall

// ---- helpers --------------------------------------------------------------------------------
module rbox(s, r = 1) hull() for (x = [r, s[0]-r], y = [r, s[1]-r]) translate([x, y, 0]) cylinder(r = r, h = s[2]);
module hex(f, h) cylinder(d = f / cos(30), h = h, $fn = 6);
module nut_pocket_top(z_top) translate([0, 0, z_top - m3_nut_h]) hex(m3_nut_f, m3_nut_h + 0.01);
module bolt(len, head = 3) color("#9aa0a6") { translate([0, 0, -head]) cylinder(d = 5.5, h = head); cylinder(d = 3, h = len); }
module nut() color("#b7bbc0") difference() { hex(5.5, 2.4); translate([0,0,-1]) cylinder(d = 3, h = 5); }
module standoff() color("#b7bbc0") { difference() { hex(5.5, so_len); cylinder(d = 3, h = 20); }
                                      translate([0,0,-6]) cylinder(d = 3, h = 6); }
INK = "#2b2b2b";   // black PLA, drawn a little lighter so edges read

// ---- the Feather (for the renders) ----------------------------------------------------------
module feather() translate([0, pcb_w, pcb_z1]) rotate([180, 0, 0]) {
  color("#0b3d2e") difference() {
    rbox([pcb_l, pcb_w, pcb_t], 1.5);
    for (h = [[2.54, 2.54], [2.54, 20.32]]) translate([h[0], h[1], -1]) cylinder(d = 2.5, h = 4);
    for (h = [[48.26, 1.8415], [48.26, 20.955]]) translate([h[0], h[1], -1]) cylinder(d = 2.2, h = 4);
    for (i = [0:15]) translate([6.35 + i*2.54, 1.27, -1]) cylinder(d = 1.0, h = 4);
    for (i = [0:11]) translate([16.51 + i*2.54, 21.59, -1]) cylinder(d = 1.0, h = 4);
  }
  up = pcb_t;
  color("#c8c8cc") translate([module_x0, 3.73, up]) cube([20.5, 15.4, 2.4]);           // ESP32-S3-MINI-1
  color("#b0b0b4") translate([-0.6, 11.43 - 4.47, up]) cube([7.35, 8.94, 3.2]);        // USB-C
  color("#e8dcc0") translate([10.922 - 3.95, 19.3675 - 4.0, up]) cube([7.9, 7.6, jst_h]); // JST PH (battery)
  color("#e8dcc0") translate([22.352 - 2.1, 11.43 - 3, up]) cube([4.25, 6, 2.95]);     // STEMMA QT
  for (b = [[10.795, 5.1435], [20.955, 5.207]]) {
    color("#d0d0d0") translate([b[0] - 2.1, b[1] - 1.4, up]) cube([4.2, 2.8, 1.5]);
    color("#555") translate([b[0], b[1], up]) cylinder(d = 1.6, h = 1.9);
  }
  color("#f4f4f4") translate([29.4005 - 0.75, 8.128 - 0.75, up]) cube([1.5, 1.5, 0.9]);  // NeoPixel
  color("#ff9a2e") translate([1.4605 - 0.4, 5.461 - 0.8, up]) cube([0.8, 1.6, 0.6]);    // CHG LED
}

// ---- feather plate --------------------------------------------------------------------------
module feather_plate() {
  difference() {
    union() {
      hull() {
        translate([-1.0, -0.75, 0]) cube([lip_x + 2.4 + 1.0, pcb_w + 1.5, plate_t]);
        for (b = fboss) translate([b[0], b[1], 0]) cylinder(d = boss_d, h = plate_t);
      }
      for (y = post_y) translate([post_x, y, 0]) cylinder(d = post_d, h = so_floor + 4);   // standoff posts
      for (b = fboss) translate([b[0], b[1], 0]) cylinder(d = boss_d, h = boss_h);
      for (h = hole_u) translate([h[0], h[1], 0]) {                                      // rests + pins
        cylinder(d = 3.8, h = pcb_z0);
        cylinder(d = 2.2, h = pcb_z1 + 1.0, $fn = 24);
      }
      for (h = hole_f) translate([h[0], h[1], 0]) cylinder(d = 3.0, h = pcb_z0);
      // antenna end: wall and a lip whose underside rises at 45 deg (prints without support)
      translate([lip_x, 3, 0]) cube([2.4, pcb_w - 6, pcb_z1 + 2.5]);
      translate([0, 3 + pcb_w - 6, 0]) rotate([90, 0, 0]) linear_extrude(pcb_w - 6)
        polygon([[lip_x, pcb_z1 - 0.5], [lip_x, pcb_z1 + 2.5], [lip_x - 2.3, pcb_z1 + 2.5], [lip_x - 2.3, pcb_z1 + 1.8]]);
      // NeoPixel light tube, notched where the module hangs down
      difference() {
        translate([neo[0], neo[1], 0]) cylinder(d = 3.8, h = pcb_z0 - 0.9 - 0.3);
        translate([module_x0 - 0.3, -50, pcb_z0 - 2.4 - 0.3]) cube([30, 100, 10]);
      }
    }
    // battery connector and its plug: cut through, open to the -Y edge so the plug slides in
    translate([jst[0] - 4.6, -60, -1]) cube([9.2, 60 + jst[1] + 4.3, 20]);
    // under the USB-C shell
    translate([-2, fy(15.9) - 0.5, plate_t - 0.5]) cube([9.5, 8.94 + 1, 1]);
    // plunger guides with a 45 deg seat for the flange
    for (b = [reset, boot]) translate([b[0], b[1], 0]) {
      translate([0, 0, -1]) cylinder(d = 3.6, h = plate_t + 2);
      translate([0, 0, plate_t - 1.8]) cylinder(d1 = 3.6, d2 = 6.4, h = 1.4);
      translate([0, 0, plate_t - 0.41]) cylinder(d = 6.4, h = 1);
    }
    translate([neo[0], neo[1], -1]) cylinder(d = 2.2, h = 20);
    translate([chg[0], chg[1], -1]) cylinder(d = 2.0, h = 20);
    for (b = fboss) translate([b[0], b[1], 0]) { translate([0,0,-1]) cylinder(d = m3_clear, h = 10); nut_pocket_top(boss_h + 0.01); }
    for (y = post_y) translate([post_x, y, 0]) {
      translate([0, 0, -1]) cylinder(d = m3_clear, h = 20);
      translate([0, 0, so_floor]) rotate([0, 0, 30]) hex(so_f, 20);
    }
  }
}

module clamp() {
  difference() {
    translate([0, 0, pcb_z1]) hull() {
      translate([-0.3, 0, 0]) cube([5.3, pcb_w, clamp_t]);
      for (y = post_y) translate([post_x, y, 0]) cylinder(d = post_d, h = clamp_t);
    }
    for (y = post_y) translate([post_x, y, 0]) cylinder(d = m3_clear, h = 30);
    for (h = hole_u) translate([h[0], h[1], pcb_z1 - 1]) cylinder(d = 2.8, h = 1 + 1.4);   // pin tips
    translate([-1, 4.5, pcb_z1 - 1]) cube([7, 14, 2]);    // clear the USB-C shell's soldered legs
  }
}

module plunger(poke) {   // printed standing on its tip
  tip_z  = -board_t - poke;
  stem_z = pcb_z0 - 1.9 - 0.3;    // 0.3 mm short of the button: no preload
  translate([0, 0, tip_z]) hull() { cylinder(d = 2.6, h = 0.01); translate([0,0,0.4]) cylinder(d = 3.2, h = 0.01); }
  translate([0, 0, tip_z + 0.4]) cylinder(d = 3.2, h = plate_t - 1.8 - tip_z - 0.4 + 0.01);
  translate([0, 0, plate_t - 1.8]) cylinder(d1 = 3.2, d2 = 6.0, h = 1.4);
  translate([0, 0, plate_t - 0.4]) cylinder(d = 6.0, h = 0.4);
  translate([0, 0, plate_t]) cylinder(d = 2.6, h = stem_z - plate_t);
}

// ---- battery tray ---------------------------------------------------------------------------
tin    = [bat[0] + 4.5, bat[1] + 1.0, bat[2] + 0.8];   // cavity; extra length for the click bump
t_rail = 2.2;
t_top  = wall + tin[2] + 3.0;
tboss  = [[8, -5], [tin[0] - 12, -5], [8, tin[1] + 5], [tin[0] - 34, tin[1] + 5]];

module tray() {   // closed end at x=-wall, mouth at x=tin[0]; click wall on +y
  beam_l = 26; slit = 0.8;
  difference() {
    union() {
      translate([-wall, -wall, 0]) cube([tin[0] + wall, tin[1] + 2*wall, t_top]);
      for (b = tboss) hull() {
        translate([b[0], b[1], 0]) cylinder(d = boss_d, h = boss_h);
        translate([b[0] - boss_d/2, b[1] < 0 ? -wall : tin[1], 0]) cube([boss_d, wall, boss_h]);
      }
      for (dy = [0, 3.4]) translate([tin[0] - 7, -wall - 4.6 - dy, 0]) cube([3, 1.6, 9]);   // wire comb
      translate([tin[0] - 7, -wall - 4.6 - 3.4, 0]) cube([3, 3.4 + 1.6, 1.6]);
    }
    translate([0, 0, wall]) cube([tin[0] + 1, tin[1], tin[2]]);                  // cavity, open mouth
    translate([0, t_rail, wall]) cube([tin[0] + 1, tin[1] - 2*t_rail, 20]);       // open top between rails
    for (s = [0, 1]) translate([0, s * tin[1], 0]) rotate([0, 90, 0]) linear_extrude(tin[0] + 1)
      polygon([[-(wall + tin[2]), 0], [-(wall + tin[2] + t_rail), (s ? -1 : 1) * t_rail], [-(wall + tin[2]), (s ? -1 : 1) * t_rail]]);
    // free the click beam (+y wall near the mouth): slit in the floor along it, and the root stays
    translate([tin[0] - beam_l, tin[1] - slit, -1]) cube([beam_l + 1, slit, wall + 2]);
    for (b = tboss) translate([b[0], b[1], 0]) { translate([0,0,-1]) cylinder(d = m3_clear, h = 10); nut_pocket_top(boss_h + 0.01); }
  }
  // click bump on the beam's inside face, 45 deg both ways, behind the battery's end
  translate([bat[0] + 0.3, tin[1] + 0.01, wall + 1]) linear_extrude(tin[2] - 2)
    polygon([[0, 0], [1.8, -1.8], [3.6, 0]]);
}

module battery(at = 0) translate([at, 0, 0]) {
  color("#c9ccd1") translate([0, (tin[1] - bat[1])/2, wall]) rbox(bat, 1.5);
  color("#c22") translate([bat[0], 3.0, wall + 3]) cube([14, 1.2, 1.2]);
  color("#222") translate([bat[0], 4.4, wall + 3]) cube([14, 1.2, 1.2]);
}

// ---- USB-C extension holder -----------------------------------------------------------------
// The female end lies at usb_angle with its lower front corner on the backboard at x=0, facing -x.
ua = usb_angle; uW = usbf[0]; uT = usbf[1]; uL = usbf[2];
cheek = 6;
u_screw_x = 6;      // strap screws (vertical); low on the slope keeps the strap short
function cheek_z() = top_z(u_screw_x) - 0.5;
function strap_cb() = cheek_z() + 2.5;      // strap screw heads sit this high
function top_z(x) = (x + uT * sin(ua)) * tan(ua) + uT / cos(ua);   // the female end's top face at x
module female_end(c = 0) rotate([0, -ua, 0]) translate([-c, 0, uT/2]) rotate([0, 90, 0])   // oval (stadium) section
  hull() for (s = [-1, 1]) translate([0, s * (uW - uT)/2, 0]) cylinder(d = uT + 2*c, h = uL + 2*c);

module usb_cradle() {
  x1 = uL * cos(ua);
  difference() {
    union() {
      // wedge + cheeks, up to just under the female end's top face
      intersection() {
        translate([0, -uW/2 - cheek, 0]) cube([x1, uW + 2*cheek, cheek_z()]);
        rotate([0, -ua, 0]) translate([-20, -50, -20]) cube([uL + 40, 100, 20 + uT - 0.5]);
      }
      for (s = [-1, 1]) translate([u_screw_x, s * (uW/2 + cheek + 4.5), 0]) cylinder(d = boss_d, h = boss_h);
      for (s = [-1, 1]) translate([u_screw_x - boss_d/2, s > 0 ? uW/2 + cheek - 1 : -(uW/2 + cheek + 4.5), 0]) cube([boss_d, 5.5, boss_h]);
    }
    female_end(0.3);
    for (s = [-1, 1]) translate([u_screw_x, s * (uW/2 + cheek + 4.5), 0]) {
      translate([0,0,-1]) cylinder(d = m3_clear, h = 10); nut_pocket_top(boss_h + 0.01);
    }
    for (s = [-1, 1]) translate([u_screw_x, s * (uW/2 + cheek/2), 0]) {          // strap screws: hole + nut slot
      translate([0, 0, 2]) cylinder(d = 3.2, h = 40);
      translate([0, 0, cheek_z() - 6.5]) rotate([0,0,30]) hex(m3_nut_f, m3_nut_h + 0.1);
      translate([-m3_nut_f/2, s > 0 ? 0 : -10, cheek_z() - 6.5]) cube([m3_nut_f, 10, m3_nut_h + 0.1]);
    }
  }
}

module usb_strap() {   // print flat top face down
  w = 10; z_top = top_z(u_screw_x + w/2) + 2.5;
  difference() {
    intersection() {
      translate([u_screw_x - w/2, -uW/2 - cheek, 0]) cube([w, uW + 2*cheek, z_top]);
      rotate([0, -ua, 0]) translate([-20, -50, uT - 0.3]) cube([uL + 40, 100, 30]);   // 0.3 squeeze
    }
    for (s = [-1, 1]) translate([u_screw_x, s * (uW/2 + cheek/2), 0]) {
      cylinder(d = m3_clear, h = 40);
      translate([0, 0, strap_cb()]) cylinder(d = 6.4, h = 40);     // counterbore for the head
    }
  }
}

module usb_parts(e = 0) {
  color(INK) usb_cradle();
  translate([0, 0, 22*e]) color(INK) usb_strap();
  color("#d8d8dc") female_end();
  color("#333") rotate([0, -ua, 0]) translate([uL, 0, uT/2]) rotate([0, 90, 0]) cylinder(d = 4, h = 15);
  for (s = [-1, 1]) translate([u_screw_x, s * (uW/2 + cheek/2), strap_cb() + 30*e]) rotate([180,0,0]) bolt(10);
  for (s = [-1, 1]) translate([u_screw_x, s * (uW/2 + cheek + 4.5), 0]) {
    translate([0, 0, -board_t - 30*e]) bolt(8);
    translate([0, 0, boss_h - 2.4 + 12*e]) nut();
  }
}
// the slot the charging plug passes through, in the cradle's frame
module usb_slot() {
  vmin = uT/2 - usb_plug[1]/2; vmax = uT/2 + usb_plug[1]/2;
  x0 = -board_t / tan(ua) - vmax / sin(ua) - 0.5; x1 = -vmin / sin(ua) + 0.5;
  translate([x0, -usb_plug[0]/2 - 0.5, -board_t - 1]) cube([x1 - x0, usb_plug[0] + 1, board_t + 2]);
}

// ---- layout on the backboard ----------------------------------------------------------------
tray_at   = [56, -30, 0];        // tray turned 180 deg: mouth faces the Feather's battery plug
usb_at    = [-42, 11.43, 0];      // female end faces away from the Feather; its cable heads back towards it
usb_rot   = 0;

module backboard(origin = [-62, -82], size = [130, 130]) color("#b98f5e", 0.55) difference() {
  translate([origin[0], origin[1], -board_t]) cube([size[0], size[1], board_t]);
  for (b = [reset, boot]) translate([b[0], b[1], -5]) cylinder(d = 4, h = 10);
  for (l = [neo, chg]) translate([l[0], l[1], -5]) cylinder(d = 3, h = 10);
  for (b = fboss) translate([b[0], b[1], -5]) cylinder(d = m3_clear, h = 10);
  for (y = post_y) translate([post_x, y, -5]) cylinder(d = m3_clear, h = 10);
  translate(tray_at) rotate([0,0,180]) for (b = tboss) translate([b[0], b[1], -5]) cylinder(d = m3_clear, h = 10);
  translate(usb_at) rotate([0,0,usb_rot]) {
    usb_slot();
    for (s = [-1, 1]) translate([u_screw_x, s * (uW/2 + cheek + 4.5), -5]) cylinder(d = m3_clear, h = 10);
  }
}

module electronics(e = 0, tray_too = true, usb_too = true) {
  for (b = fboss) translate([b[0], b[1], -board_t - 30*e]) bolt(8);
  for (b = fboss) translate([b[0], b[1], boss_h - 2.4 + 12*e]) nut();
  color(INK) feather_plate();
  for (y = post_y) translate([post_x, y, so_floor + 18*e]) standoff();
  for (y = post_y) translate([post_x, y, -board_t - 2.4 - 30*e]) nut();
  translate([reset[0], reset[1], 10*e]) color("#3d7be0") plunger(poke_reset);
  translate([boot[0], boot[1], 10*e]) color("#e0603d") plunger(poke_boot);
  translate([0, 0, 28*e]) feather();
  translate([0, 0, 42*e]) color(INK) clamp();
  for (y = post_y) translate([post_x, y, pcb_z1 + clamp_t + 54*e]) rotate([180,0,0]) bolt(6);
  if (tray_too) translate(tray_at) rotate([0,0,180]) {
    color(INK) tray();
    battery(55*e);
    for (b = tboss) translate([b[0], b[1], boss_h - 2.4 + 12*e]) nut();
    for (b = tboss) translate([b[0], b[1], -board_t - 30*e]) bolt(8);
  }
  if (usb_too) translate(usb_at) rotate([0,0,usb_rot]) usb_parts(e);
}

module scene(e = 0, tray_too = true, usb_too = true) {
  translate([0, 0, -16*e]) backboard();
  electronics(e, tray_too, usb_too);
}

if (part == "assembly") scene(0);
else if (part == "exploded") scene(1);
else if (part == "section") intersection() { scene(0, false, false); color("#e0533d") translate([-8, reset[1] - 45, -12]) cube([66, 45, 40]); }
else if (part == "usb_section") intersection() {
  union() { usb_parts(); color("#b98f5e") difference() { translate([-35, -25, -board_t]) cube([70, 50, board_t]); usb_slot(); } }
  color("#e0533d") translate([-40, 0, -12]) cube([80, 40, 50]);
}
else if (part == "outside") scene(0);
else if (part == "feather_plate") feather_plate();
else if (part == "clamp") translate([0, 0, pcb_z1 + clamp_t]) mirror([0,0,1]) clamp();
else if (part == "plunger_boot") translate([0,0,board_t + poke_boot]) plunger(poke_boot);
else if (part == "plunger_reset") translate([0,0,board_t + poke_reset]) plunger(poke_reset);
else if (part == "tray") tray();
else if (part == "usb_cradle") usb_cradle();
else if (part == "usb_strap") let(zt = top_z(u_screw_x + 5) + 2.5) translate([0,0,zt]) mirror([0,0,1]) usb_strap();
