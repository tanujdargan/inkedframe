// inkedframe: pictures for the assembly guide (ASSEMBLY.md). Nothing here is printed.
// openscad -o step3.png -D step=3 guide.scad
include <mount.scad>
include <faceplate.scad>
part = "none";
step = 1;
$fn = 40;

ARROW = "#e8590c";
module arrow(from, to, d = 1.6) {   // straight arrow, head at `to`
  v = to - from; l = norm(v); b = acos(v[2] / l); c = atan2(v[1], v[0]);
  color(ARROW) translate(from) rotate([0, b, c]) {
    cylinder(d = d, h = l - 4);
    translate([0, 0, l - 4]) cylinder(d1 = d * 2.6, d2 = 0, h = 4);
  }
}
module ghost() color("#2b2b2b", 0.25) children();

module board() backboard();
module plate_done() {
  color(INK) feather_plate();
  for (y = post_y) translate([post_x, y, so_floor]) standoff();
  for (b = fboss) translate([b[0], b[1], boss_h - 2.4]) nut();
  for (y = post_y) translate([post_x, y, -board_t - 2.4]) nut();
}
module plungers_done() {
  translate([reset[0], reset[1], 0]) color("#3d7be0") plunger(poke_reset);
  translate([boot[0], boot[1], 0]) color("#e0603d") plunger(poke_boot);
}
module clamp_done() { color(INK) clamp(); for (y = post_y) translate([post_x, y, pcb_z1 + clamp_t]) rotate([180,0,0]) bolt(6); }
module tray_done(bat_at = 0) translate(tray_at) rotate([0,0,180]) {
  color(INK) tray(); battery(bat_at);
  for (b = tboss) translate([b[0], b[1], boss_h - 2.4]) nut();
}
module usb_done() translate(usb_at) rotate([0,0,usb_rot]) usb_parts(0);

if (step == 1) {          // drill, using the parts as templates
  board();
  ghost() feather_plate();
  ghost() translate(tray_at) rotate([0,0,180]) tray();
  ghost() translate(usb_at) rotate([0,0,usb_rot]) usb_cradle();
}
else if (step == 2) {     // feather plate, standoffs, bolts
  board();
  translate([0, 0, 14]) color(INK) feather_plate();
  for (y = post_y) { translate([post_x, y, so_floor + 34]) standoff(); arrow([post_x, y, 48], [post_x, y, 42]); }
  for (y = post_y) translate([post_x, y, -board_t - 2.4 - 14]) nut();
  for (b = fboss) { translate([b[0], b[1], -board_t - 16]) bolt(8); translate([b[0], b[1], boss_h + 22]) nut(); }
  arrow([24, -30, 30], [24, -30, 18]);
}
else if (step == 3) {     // plungers
  board(); plate_done();
  translate([reset[0], reset[1], 16]) color("#3d7be0") plunger(poke_reset);
  translate([boot[0], boot[1], 16]) color("#e0603d") plunger(poke_boot);
  for (b = [reset, boot]) arrow([b[0], b[1], 32], [b[0], b[1], 23]);
}
else if (step == 4) {     // feather: far end under the lip, then lower the USB end
  board(); plate_done(); plungers_done();
  translate([pcb_l, 0, pcb_z1]) rotate([0, 12, 0]) translate([-pcb_l, 0, -pcb_z1]) feather();
  arrow([-6, pcb_w / 2, 24], [-6, pcb_w / 2, 13]);
  arrow([pcb_l - 14, pcb_w + 8, 16], [pcb_l - 2, pcb_w + 8, 12]);
}
else if (step == 5) {     // clamp
  board(); plate_done(); plungers_done(); feather();
  translate([0, 0, 14]) color(INK) clamp();
  for (y = post_y) { translate([post_x, y, pcb_z1 + clamp_t + 30]) rotate([180,0,0]) bolt(6); arrow([post_x + 9, y, 30], [post_x + 9, y, 22]); }
}
else if (step == 6) {     // battery tray, battery clicks in
  board(); plate_done(); plungers_done(); feather(); clamp_done();
  tray_done(45);
  translate(tray_at) rotate([0,0,180]) arrow([118, 18, 14], [98, 18, 14], 2);
}
else if (step == 7) {     // USB-C extension
  board(); plate_done(); plungers_done(); feather(); clamp_done(); tray_done();
  translate(usb_at) rotate([0,0,usb_rot]) {
    color(INK) usb_cradle();
    color("#d8d8dc") female_end();
    translate([0, 0, 18]) color(INK) usb_strap();
    for (s = [-1, 1]) translate([u_screw_x, s * (uW/2 + cheek/2), strap_cb() + 34]) rotate([180,0,0]) bolt(10);
    arrow([u_screw_x, 0, 46], [u_screw_x, 0, 38]);
  }
}
else if (step == 8) {     // inside, done
  board(); plate_done(); plungers_done(); feather(); clamp_done(); tray_done(); usb_done();
}
else if (step == 9) {     // panel into the mat's pocket, then the clips (mat face down)
  color("#f4f2ec") mat();
  translate([0, 0, 22]) panel_model();
  for (c = clips) translate([c[0], c[1], t_back + 36]) rotate([0, 0, c[2]]) color("#2b2b2b") panel_clip();
  arrow([mat[0] / 2, mat[1] / 2, 40], [mat[0] / 2, mat[1] / 2, 28], 3);
}
else if (step == 10) {    // name plate into the front recess (mat face up)
  rotate([0, 180, 0]) {
    color("#f4f2ec") mat(); panel_model();
    translate([name0[0], name0[1], -16]) {
      color("#f4f2ec") name_plate(false);
      translate([name[0]/2, name[1]/2, -0.02]) color("#1d1d1d") linear_extrude(0.4) mirror([1,0]) name_art(false);
    }
    arrow([name0[0] + name[0]/2, name0[1] + name[1]/2, -30], [name0[0] + name[0]/2, name0[1] + name[1]/2, -18], 2.5);
  }
}
else if (step == 11) rotate([0, 180, 0]) {   // into the frame, front up: acrylic, mat + panel, backboard
  color("#cfe8ff", 0.35) translate([0, 0, -2 - 45]) cube([180, 135, 2]);
  front_assembly(0);
  translate([100, 50, 30 + 45]) rotate([180, 0, 0]) {
    backboard(origin = [-100, 50 - 135], size = [180, 135]);
    electronics(0);
  }
}
