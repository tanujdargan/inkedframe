// inkedframe: the whole stack, for checking depth and layout. Nothing here is printed.
//   acrylic | mat + panel | ~27 mm | backboard with the electronics on its inside face
use <mount.scad>
use <faceplate.scad>

part  = "frame";        // frame | exploded | side
depth = 30;             // acrylic's back face to the backboard's inside face
mount_at = [100, 50];   // where the mount's origin (the Feather's USB-end corner) lands, in mat coordinates
$fn = 32;

module stack(e = 0) {
  color("#cfe8ff", 0.25) translate([0, 0, -2 - 40*e]) cube([180, 135, 2]);   // acrylic
  front_assembly(0);
  translate([mount_at[0], mount_at[1], depth + 40*e]) rotate([180, 0, 0]) {
    backboard(origin = [-mount_at[0], mount_at[1] - 135], size = [180, 135]);
    electronics(0);
  }
}

if (part == "frame") stack(0);
else if (part == "exploded") stack(1);
else if (part == "side") intersection() {   // cut through the Feather's buttons
  stack(0);
  color("#e0533d") translate([-10, mount_at[1] - 17.7, -10]) cube([200, 40, 60]);
}
