// inkedframe v2: pictures for the assembly guide (printed backboard, mitred mat). Nothing here is printed.
// openscad -o step3.png -D step=3 guide2.scad
include <backboard.scad>
include <faceplate.scad>
part = "none";
step = 1;
$fn = 36;
SD = "stl/";

ARROW = "#e8590c";
module arrow(from, to, d = 1.6) {
  v = to - from; l = norm(v); b = acos(v[2] / l); c = atan2(v[1], v[0]);
  color(ARROW) translate(from) rotate([0, b, c]) { cylinder(d = d, h = l - 4); translate([0, 0, l - 4]) cylinder(d1 = d * 2.6, d2 = 0, h = 4); }
}
module screw_down(p, len, lift = 0) translate([p[0], p[1], p[2] + lift]) rotate([180, 0, 0]) bolt(len);   // head at p, pointing down

// nut positions (board coordinates)
function seam_pts() = [for (y = seam_y, dx = seam_dx) [seam + dx, y]];
function tray_pts() = [for (b = tray_boss) let(q = [-b[1], b[0]]) [49.5 + q[0], 41.6 + q[1]]];
function port_pts() = [for (p = pflange) [port_at[0] - p[1], port_at[1] + p[0]]];
module outside_nuts(lift = 0) for (p = concat(seam_pts(), tray_pts(), port_pts())) translate([p[0], p[1], -T - 2.4 - lift]) nut();

module board_l() color("#3b3b3b") translate([0, 0, -T]) import(str(SD, "board_left.stl"));
module board_r() color("#454545") translate([0, 0, -T]) import(str(SD, "board_right.stl"));
module splice_s() color("#555") import(str(SD, "splice.stl"));
module feather_done() at_feather() {
  for (b = [[reset, poke_reset, "#3d7be0"], [boot, poke_boot, "#e0603d"]]) translate([b[0][0], b[0][1], 0]) color(b[2]) plunger3(b[1]);
  translate([0, 0, z1 - mz1]) feather();
  translate([0, 0, z1 - mz1]) color(INK) clamp();
}
module tray_done() at_tray() translate([0, 0, 0.6]) { color(INK) tray(); battery(0); }
module port_done() at_port() { color(INK) port_block(); female_model(); }
module zip_tie() color("#f2f2f2") along_axis() translate([fL + 3.6, 0, 0]) rotate([0, 90, 0]) rotate_extrude($fn = 30) translate([cable_d / 2 + 0.4, 0]) square([0.8, 2.5]);
module strip(i) color("#f4f2ec") import(str(SD, ["strip_bottom.stl", "strip_right.stl", "strip_top.stl", "strip_left.stl"][i]));

if (step == 1) {          // nuts into the outside pockets (board shown from the outside)
  board_l(); board_r(); outside_nuts(10);
  for (p = [seam_pts()[0], tray_pts()[0], port_pts()[0]]) arrow([p[0], p[1], -T - 22], [p[0], p[1], -T - 15]);
}
else if (step == 2) {     // join the halves with the strip
  board_l(); board_r(); outside_nuts();
  translate([0, 0, 12]) splice_s();
  for (p = seam_pts()) screw_down([p[0], p[1], 3], 6, 24);
  arrow([seam, 75, 26], [seam, 75, 17], 2.4);
}
else if (step == 3) {     // plungers and the clamp nuts
  board_l(); board_r(); splice_s();
  at_feather() {
    for (b = [[reset, poke_reset, "#3d7be0"], [boot, poke_boot, "#e0603d"]]) { translate([b[0][0], b[0][1], 14]) color(b[2]) plunger3(b[1]);
      arrow([b[0][0], b[0][1], 26], [b[0][0], b[0][1], 19]); }
    for (i = [0, 1]) translate([post_x, post_y[i] + (i == 0 ? -12 : 12), 1.2]) rotate([0, 0, 30]) nut();
    arrow([post_x, post_y[0] - 22, 2.5], [post_x, post_y[0] - 15, 2.5]);
    arrow([post_x, post_y[1] + 22, 2.5], [post_x, post_y[1] + 15, 2.5]);
  }
}
else if (step == 4) {     // Feather: antenna end under the lip, then the USB end down; clamp
  board_l(); board_r(); splice_s();
  at_feather() {
    for (b = [[reset, poke_reset, "#3d7be0"], [boot, poke_boot, "#e0603d"]]) translate([b[0][0], b[0][1], 0]) color(b[2]) plunger3(b[1]);
    translate([pcb_l, 0, z1]) rotate([0, 12, 0]) translate([-pcb_l, 0, -z1]) translate([0, 0, z1 - mz1]) feather();
    translate([0, 0, z1 - mz1 + 26]) color(INK) clamp();
    for (y = post_y) screw_down([post_x, y, z1 + 3], 8, 36);
    arrow([-4, pcb_w / 2, 26], [-4, pcb_w / 2, 16]);
  }
}
else if (step == 5) {     // battery tray, battery, plug
  board_l(); board_r(); splice_s(); feather_done();
  at_tray() translate([0, 0, 0.6 + 16]) { color(INK) tray(); battery(0); }
  at_tray() for (b = tray_boss) screw_down([b[0], b[1], 0.6 + 2.4], 6, 34);
  arrow([30, 75, 30], [30, 75, 20], 2.4);
}
else if (step == 6) {     // USB-C extension into the port block, zip tie, block onto the board
  board_l(); board_r(); splice_s(); feather_done(); tray_done();
  at_port() translate([0, 0, 16]) { color(INK) port_block(); female_model(); zip_tie(); }
  at_port() for (p = pflange) screw_down([p[0], p[1], 3], 6, 34);
  arrow([port_at[0], port_at[1] + 10, 34], [port_at[0], port_at[1] + 10, 24], 2.4);
}
else if (step == 7) {     // inside, done
  board_l(); board_r(); splice_s(); feather_done(); tray_done(); port_done();
}
else if (step == 8) {     // mat strips face down, keys from the back
  for (i = [0:3]) let(d = [[0, -3], [3, 0], [0, 3], [-3, 0]][i]) translate([d[0], d[1], 0]) strip(i);
  for (k = [0:3]) let(kp = key_place(k)) {
    color("#e0603d") translate([kp[0][0], kp[0][1], t_back - 1.4 + 10]) rotate([0, 0, kp[1] + 90]) linear_extrude(key_t) key_shape();
    arrow([kp[0][0], kp[0][1], 24], [kp[0][0], kp[0][1], 14]);
  }
}
else if (step == 9) {     // panel into the pocket, clips
  for (i = [0:3]) strip(i);
  for (k = [0:3]) let(kp = key_place(k)) color("#f4f2ec") translate([kp[0][0], kp[0][1], t_back - 1.4]) rotate([0, 0, kp[1] + 90]) linear_extrude(key_t) key_shape();
  translate([0, 0, 18]) panel_model();
  for (c = clips) translate([c[0], c[1], t_back + 32]) rotate([0, 0, c[2]]) color("#2b2b2b") panel_clip();
  arrow([mat[0] / 2, mat[1] / 2, 36], [mat[0] / 2, mat[1] / 2, 24], 3);
}
else if (step == 10) rotate([0, 180, 0]) {   // name plate into the front
  for (i = [0:3]) strip(i);
  panel_model();
  translate([name0[0], name0[1], -14]) {
    color("#f4f2ec") name_plate(false);
    translate([name[0]/2, name[1]/2, -0.02]) color("#1d1d1d") linear_extrude(0.4) mirror([1, 0]) name_art(false);
  }
  arrow([name0[0] + name[0]/2, name0[1] + name[1]/2, -28], [name0[0] + name[0]/2, name0[1] + name[1]/2, -16], 2.5);
}
else if (step == 11) rotate([0, 180, 0]) {   // into the frame: acrylic, mat + panel, backboard
  color("#cfe8ff", 0.35) translate([0, 0, -2 - 40]) cube([202, 148, 2]);
  for (i = [0:3]) strip(i);
  panel_model();
  translate([name0[0], name0[1], 0]) { color("#f4f2ec") name_plate(false);
    translate([name[0]/2, name[1]/2, 0.39]) color("#1d1d1d") linear_extrude(0.4) mirror([1, 0]) name_art(false); }
  // board: centred on the mat, inside face towards the panel
  translate([mat[0] / 2, mat[1] / 2, 30 + 40]) mirror([0, 0, 1]) mirror([1, 0, 0]) translate([-B[0] / 2, -B[1] / 2, 0]) {
    board_l(); board_r(); splice_s(); feather_done(); tray_done(); port_done();
  }
}
else if (step == 12) {    // desk stand, landscape socket (seen from the outside)
  board_l(); board_r();
  c = (sock_L[0] + sock_L[1]) / 2;
  translate([c[0], c[1], -T - 10]) mirror([0, 0, 1]) { color(INK) stand_base(); }
  translate([c[0], c[1], -T - 26]) mirror([0, 0, 1]) color("#3a3a3a") leg_open();
  for (s = sock_L) translate([s[0], s[1], -T - 3 - 22]) bolt(10);
  arrow([c[0], c[1], -T - 40], [c[0], c[1], -T - 30], 2.4);
}
else if (step == 13) {    // hanging: keyholes (outside)
  board_l(); board_r();
  for (k = keys) for (d = k[1]) let(p = k[0]) arrow([p[0], p[1], -T - 18], [p[0], p[1], -T - 6], 2.2);
}
