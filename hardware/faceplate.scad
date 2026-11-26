// inkedframe: the printed mat (face plate) that replaces the paper mat behind the frame's acrylic,
// and the swappable name plate.
//
//   mat          white PLA, printed face down on the smooth plate (0.4 mm nozzle, 0.2 mm layers).
//                The window has a 45 degree bevel like a cut mat. The panel drops into a pocket on the
//                back, so it is always lined up with the window, and six small clips hold it there.
//                The window is centred: equal borders left and right, and top and bottom.
//                The name plate sits in a 0.8 mm pocket in the front and the acrylic keeps it in; lift it
//                out with a piece of tape.
//   name_h / name_v
//                80 x 12 x 0.8 mm name plates: name_h reads across (landscape), name_v has the letters
//                stacked for a frame stood on its end (turn the plate round for the other way up).
//                Printed face down: the first two layers in white with the letters cut through, then a
//                pause at 0.4 mm to change to black, so the letters show black and the front stays flush.
//   panel_clip   x6, black PLA.
//
// Coordinates: Z from the front face (z=0) to the back, Y up. Seen from the front, X runs right to left
// (the model is right-handed and you look at it along +Z), so "left" below means left as seen from the front.

part = "mat";          // mat | name_h | name_v | name_blank | panel_clip | front | front_v | back
name_text   = "Sachdeva's";   // across
name_stack  = "Sachdeva";     // stacked
name_font   = "Snell Roundhand:style=Black";
name_height = 7.5;     // across: height of the lettering
stack_size  = 6.2;     // stacked: text size of each letter (the widest, "S", is then 7.9 of the plate's 10)
stack_end   = 5;       // stacked: clear space at each end of the plate
stack_upright = 14;    // stacked: degrees of slant taken out of each letter (stacked italics look like they lean over)
stroke_grow = 0.12;    // thickens the script's hairlines so they print with a 0.4 mm nozzle
$fn = 48;

// ---- measurements --------------------------------------------------------------------------
mat      = [202 - 0.3, 148 - 0.3];  // frame opening 202 x 148, less 0.15 mm all round: little room for the strips to drift apart
skin     = 1.8;                    // front thickness
panel    = [170.2, 111.2, 0.91];   // Waveshare 7.3" Spectra 6 raw panel, datasheet
active   = [160, 96];              // datasheet
act_off  = [5.1, 4.7];             // active area from the panel's left and top edges, seen from the front (datasheet;
                                   // the bottom border is 111.2 - 96 - 4.7 = 10.5)
inset    = 1.0;                    // window is this much inside the active area on every side
top_gap  = (mat[1] - (active[1] - 2*inset)) / 2; // window centred top to bottom
bevel    = skin;                   // 45 deg: as wide as the skin is thick
fpc_x    = 72.35 + 25.5/2;         // FPC centre from the panel's left edge, seen from the front (datasheet)
fpc_w    = 30;                     // notch width for the FPC (tail is 25.5)
edge_ch  = 0.4;                    // 45 deg chamfer on the front of the outer edge (under the frame's lip): takes away the
                                   // first layer's squish there, so it can't bind in the frame
relief   = 0.1;                    // the mitre faces are set back this much above the first two layers, so only the front
relief_z = 0.32;                   // edge of each joint touches and nothing behind it can hold the joint open
name     = [80, 10, 0.8];
name_gap = 0.15;                   // pocket clearance per side around the plate (plate itself is 0.1 under size)
name_pocket = false;               // false: plain smooth bottom border, no name plate
name_pd  = 0.92;                   // pocket depth: deeper than the plate because its floor is bridged and sags
name_y   = top_gap / 2;            // name plate centred in the bottom border
clear    = 0.2;                    // panel pocket clearance per side

// ---- derived --------------------------------------------------------------------------------
win    = active - 2*[inset, inset];
win0   = [(mat[0] - win[0]) / 2, mat[1] - top_gap - win[1]];   // window bottom-left
pan0   = [win0[0] - inset - (panel[0] - active[0] - act_off[0]), win0[1] + win[1] + inset + act_off[1] - panel[1]];
fpc_mx = panel[0] - fpc_x;         // the same, in model X
pocket = 1.0;                      // 0.91 panel; the clips sit 0.09 above its back
t_back = skin + pocket;            // 2.8 where there is no panel
name0  = [mat[0]/2 - name[0]/2, name_y - name[1]/2];
clips  = [  // [x, y, rotation]: the clip reaches over the panel edge
  [pan0[0] + 35, pan0[1] + panel[1], 0], [pan0[0] + panel[0] - 35, pan0[1] + panel[1], 0],
  [pan0[0] + 30, pan0[1], 180], [pan0[0] + panel[0] - 30, pan0[1], 180],
  [pan0[0], pan0[1] + panel[1]/2, 90], [pan0[0] + panel[0], pan0[1] + panel[1]/2, 270]];

module rrect(s, r) translate([r, r]) offset(r) square(s - [2*r, 2*r]);

module mat() difference() {
  union() {   // outer block with the front chamfer, in 0.1 steps
    translate([0, 0, edge_ch]) linear_extrude(t_back - edge_ch) square(mat);
    for (z = [0 : 0.1 : edge_ch - 0.05]) let(d = edge_ch - z - 0.1)
      translate([d, d, z]) cube([mat[0] - 2*d, mat[1] - 2*d, 0.1 + 0.001]);
  }
  // window with a 45 deg bevel: larger at the front
  hull() {
    translate([0, 0, -0.01]) linear_extrude(0.01) translate(win0 - [bevel, bevel]) square(win + [2*bevel, 2*bevel]);
    translate([0, 0, skin]) linear_extrude(0.01) translate(win0) square(win);
  }
  translate([0, 0, skin]) linear_extrude(10) translate(win0) square(win);
  // panel pocket and the FPC notch
  translate([0, 0, skin]) linear_extrude(10) translate(pan0 - [clear, clear]) square([panel[0], panel[1]] + [2*clear, 2*clear]);
  translate([pan0[0] + fpc_mx - fpc_w/2, pan0[1] - 6, skin]) cube([fpc_w, 7, 10]);
  // name plate pocket from the front
  if (name_pocket) translate([0, 0, -1]) linear_extrude(1 + name_pd) translate(name0 - [name_gap, name_gap]) rrect([name[0], name[1]] + 2*[name_gap, name_gap], 1 + name_gap);
  // clip pegs' holes
  for (c = clips) translate([c[0], c[1], 0]) rotate([0, 0, c[2]]) for (dx = [-3, 3]) translate([dx, 2.6, t_back - 2]) cylinder(d = 2.0, h = 3);
}

// clip: 12 x 6.3 x 1.2, two pegs into the rim, reaches 2 mm over the panel's back edge
module panel_clip() difference() {
  union() {
    translate([-6, -2, 0]) cube([12, 6.3, 1.2]);
    // pegs: tapered, 1.75 at the root to 1.45 at the tip, for the 2.0 holes (1.9 straight pegs printed too fat to go in)
    for (dx = [-3, 3]) translate([dx, 2.6, -1.8]) cylinder(d1 = 1.45, d2 = 1.75, h = 1.81);
  }
}

// the lettering as seen from the front, centred on the plate. Stacked letters are turned so they stand
// upright when the frame is turned a quarter clockwise (the name strip then runs up the left side), matching
// the backboard's portrait keyholes and stand; for the other way, put the plate in turned round.
// Letters are placed by their ink boxes (textmetrics), not their advance widths: script capitals swing far
// outside their advance, which pushed the stacked "S" into the plate edge.
function psum(v, n) = n <= 0 ? 0 : v[n - 1] + psum(v, n - 1);
module glyph_centred(t, size) let(m = textmetrics(t, size = size, font = name_font))
  translate(-(m.position + m.size / 2)) text(t, size = size, font = name_font);
module name_art(stacked = false) offset(delta = stroke_grow)
  if (!stacked) let(m = textmetrics(name_text, size = 10, font = name_font)) scale(name_height / m.size[1])
    glyph_centred(name_text, 10);
  else let(n = len(name_stack),
           ms = [for (c = name_stack) textmetrics(c, size = stack_size, font = name_font)],
           hs = [for (m = ms) m.size[1]],
           span = name[0] - 2 * stack_end,
           gap = (span - psum(hs, n)) / (n - 1))
    for (i = [0 : n - 1]) translate([-span / 2 + psum(hs, i) + i * gap + hs[i] / 2, 0]) rotate(90)
      multmatrix([[1, -tan(stack_upright), 0, 0], [0, 1, 0, 0], [0, 0, 1, 0]])   // take some of the script's slant out
        glyph_centred(name_stack[i], stack_size);

module name_plate(stacked = false) {   // face at z=0, so the lettering is mirrored in the model
  c = 0.1;
  difference() {
    linear_extrude(name[2]) translate([c, c]) rrect([name[0] - 2*c, name[1] - 2*c], 1 - c);
    translate([name[0]/2, name[1]/2, -1]) linear_extrude(1 + 0.4) mirror([1, 0]) name_art(stacked);
  }
}

module panel_model() {
  color("#e9e9e4") translate([pan0[0], pan0[1], skin]) cube(panel);
  color("#5d7db0") translate([win0[0] - inset, win0[1] - inset, skin - 0.01]) cube([active[0], active[1], 0.02]);
  color("#d9a441") translate([pan0[0] + fpc_mx - 13.5, pan0[1] - 2, skin + 0.4]) cube([27, 2.5, 0.2]);
  color("#d9a441") translate([pan0[0] + fpc_mx - 13.5, pan0[1] - 2.2, skin + 0.6]) cube([27, 0.2, 6]);
}

module front_assembly(e = 0, stacked = false) {
  color("#f4f2ec") mat();
  translate([name0[0], name0[1], -14*e]) color("#f4f2ec") name_plate(stacked);
  translate([name0[0] + name[0]/2, name0[1] + name[1]/2, 0.39 - 14*e]) color("#1d1d1d")
    linear_extrude(0.4) mirror([1,0]) name_art(stacked);
  translate([0, 0, 10*e]) panel_model();
  for (c = clips) translate([c[0], c[1], t_back + 20*e]) rotate([0, 0, c[2]]) color("#2b2b2b") panel_clip();
}

// The mat is wider than the A1 mini's bed, so it is printed as four strips mitred at the corners (outer corner to
// window corner on the front face, where the bevel's corner crease is) that drop into the frame's recess side by
// side. Each strip prints face down.
function miter_pts() = let(W = mat[0], H = mat[1], a = win0 - [bevel, bevel], b = win0 + win + [bevel, bevel],
                           ext = function(o, c) c + (c - o) * 0.3)
  [[0, 0], [W, 0], [W, H], [0, H], ext([0, 0], a), ext([W, 0], [b[0], a[1]]), ext([W, H], b), ext([0, H], [a[0], b[1]])];
module strip_shape(i) let(p = miter_pts())
  polygon(i == 0 ? [p[0], p[1], p[5], p[4]] :          // bottom
          i == 1 ? [p[1], p[2], p[6], p[5]] :          // right (as modelled; the left as seen from the front)
          i == 2 ? [p[2], p[3], p[7], p[6]] :          // top
                   [p[3], p[0], p[4], p[7]]);          // left
// Dog-bone keys lock each corner joint from the back: a pocket 1.4 mm deep straddles the joint in the solid
// border outside the panel, and a printed key presses in. The key's square shoulders hook behind the pocket's
// shoulders, and the key is a little shorter between them than the pocket is, so pressing it in pulls the joint
// shut (the chamfers on both lead it in). The front of both strips stays whole up to the joint.
// Key coordinates: X across the joint, Y along it.
key_head = [3.5, 7];     // each head: length across the joint, width
key_neck = [5, 3];       // neck: length between the heads' shoulders, width
key_t    = 1.3;          // key thickness (pocket is 1.4 deep)
key_at   = 16;           // distance along the joint from the outer corner
key_draw = 0.04;         // per side: the pocket's shoulders sit this much further out than the key's
key_end  = 0.2;          // pocket clearance at the key's ends
key_side = 0.1;          // pocket clearance along the key's sides
// sh: half the shoulder span; e: added at the ends; s: added at the sides
module key_outline(sh = key_neck[0]/2, e = 0, s = 0) let(L = key_neck[0]/2 + key_head[0] + e) {
  for (m = [-1, 1]) translate([m > 0 ? sh : -L, -key_head[1]/2 - s]) square([L - sh, key_head[1] + 2*s]);
  translate([-sh - 0.01, -key_neck[1]/2 - s]) square([2*sh + 0.02, key_neck[1] + 2*s]);
}
module key_shape() key_outline();   // the key's outline (for the pictures)
module mat_key() {   // printed flat; the bottom 0.3 of the shoulders is chamfered as a lead-in
  for (i = [0:2]) translate([0, 0, i*0.1]) linear_extrude(0.1 + 0.001) key_outline(sh = key_neck[0]/2 + 0.3 - i*0.1);
  translate([0, 0, 0.3]) linear_extrude(key_t - 0.3) key_outline();
}
function key_place(k) = let(p = miter_pts(), o = p[k], c = p[k + 4], d = (c - o) / norm(c - o)) [o + d * key_at, atan2(d[1], d[0])];
module key_pocket() let(sh = key_neck[0]/2 + key_draw) {   // its top 0.4 of the shoulders is chamfered
  linear_extrude(1.4 - 0.4 + 0.001) key_outline(sh, key_end, key_side);
  for (i = [0:3]) translate([0, 0, 1.0 + i*0.1]) linear_extrude(0.1 + (i == 3 ? 5 : 0.001)) key_outline(sh - (i + 1)*0.1, key_end, key_side);
}
module key_pockets() for (k = [0:3]) let(kp = key_place(k))
  translate([kp[0][0], kp[0][1], t_back - 1.4]) rotate([0, 0, kp[1] + 90]) key_pocket();
module mat_keyed() difference() { mat(); key_pockets(); }
// a small V notch in the middle of each strip's outer edge (hidden in the frame's recess): the slicer puts the
// wall seams in concave corners, so this keeps the seam bumps off the mitred faces where the strips meet
module seam_notch(i) let(W = mat[0], H = mat[1], c = [[W/2, 0, 0], [W, H/2, 90], [W/2, H, 180], [0, H/2, 270]][i])
  translate([c[0], c[1], -1]) rotate([0, 0, c[2]]) linear_extrude(10) polygon([[-0.6, -0.01], [0.6, -0.01], [0, 0.6]]);
// the back relief: a band 2 x relief wide along each mitre line, from relief_z up
module mitre_relief() let(p = miter_pts()) for (k = [0:3]) let(o = p[k], c = p[k + 4], d = (c - o) / norm(c - o))
  translate([0, 0, relief_z]) linear_extrude(10) hull() { translate(o - d*2) circle(r = relief, $fn = 8); translate(c + d*2) circle(r = relief, $fn = 8); }
module mat_strip(i) difference() {
  intersection() { mat_keyed(); translate([0, 0, -1]) linear_extrude(10) strip_shape(i); }
  seam_notch(i);
  mitre_relief();
}

if (part == "mat") mat();
else if (part == "strip_bottom") mat_strip(0);
else if (part == "mat_key") mat_key();
else if (part == "mat_back_keys") { color("#f4f2ec") for (i = [0:3]) mat_strip(i);
  for (k = [0:3]) let(kp = key_place(k)) color("#e0603d") translate([kp[0][0], kp[0][1], t_back - 1.4 + 6]) rotate([0, 0, kp[1] + 90]) linear_extrude(key_t) key_shape(); }
else if (part == "strip_right") mat_strip(1);
else if (part == "strip_top") mat_strip(2);
else if (part == "strip_left") mat_strip(3);
else if (part == "strips_exploded") rotate([0, 180, 0]) for (i = [0:3]) let(d = [[0, -12], [12, 0], [0, 12], [-12, 0]][i])
  translate([d[0], d[1], 0]) color("#f4f2ec") mat_strip(i);
else if (part == "name_h") name_plate(false);
else if (part == "name_v") name_plate(true);
else if (part == "name_blank") let(c = 0.1) linear_extrude(name[2]) translate([c, c]) rrect([name[0] - 2*c, name[1] - 2*c], 1 - c);
else if (part == "panel_clip") translate([0,0,1.2]) mirror([0,0,1]) panel_clip();
else if (part == "front") rotate([0, 180, 0]) front_assembly(0);   // turned so the front faces +Z
else if (part == "front_v") rotate([0, 0, -90]) rotate([0, 180, 0]) front_assembly(0, true);   // stood on its end
else if (part == "back") front_assembly(1);
