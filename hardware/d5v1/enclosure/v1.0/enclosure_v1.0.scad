// SPDX-FileCopyrightText: 2026 Lars Greiss / LG Aquatic Life
// SPDX-License-Identifier: CC-BY-NC-SA-4.0

// =====================================================================
//  Enclosure LGAL Doser 5 for board d5v1 v1.0 - flat variant
//  The board lies flat BEHIND the pumps (12 mm above the floor)
//  Two parts: base (with front panel) + lid (smooth, stackable)
//  Material: PA12 (MJF), M3 heat-set inserts
//
//  Coordinates (OpenSCAD, right-handed):
//   X: positive to the RIGHT seen from the front; KiCad x is mirrored: X = 300 - x
//   Y = 0 at the inside of the front panel, positive to the back; board: Y = y_KiCad + pcb_off
//   Z = 0 at the outside of the floor
//  Pump 1 (KiCad x 242) is on the LEFT seen from the front.
//
//  Output:  part = "base" | "lid" | "plug" | "assembly"
// =====================================================================

part = "assembly";
$fn = 48;

function MX(x) = 300 - x;

// ---------- General ----------
wall    = 2.5;
floor_t = 3;
front_t = 2.5;
lid_t   = 2.5;
corner_r = 4;          // radius of the vertical outer edges
chamfer  = 0.8;        // chamfer at the top edge of the lid and the bottom edge

// ---------- Pumps (Kamoer KPHM100-ST, data sheet 2.3.1) ----------
pump_pitch   = 55;     // head incl. tube nipples 50 mm + 5 mm gap
pump_x       = [for (i = [-2:2]) 150 + i * pump_pitch];
pump_z       = 25;
motor_len    = 50.5;
pump_cut     = 30.3;
// The Kamoer drawing shows the mounting plate FROM THE BACK (motor side).
// Hole pattern and rotation are therefore mirrored for the front view.
pump_cut_rot = 22;     // as in the drawing (from the back), counter-clockwise
pump_hole_d  = 3.5;
pump_holes   = [[-15.5, 15.5], [17.5, 8.5], [-17.5, -8.5], [15.5, -15.5]];  // as in the drawing (from the back)
pump_num_size = 2.5;   // engraved pump numbers below the heads

// ---------- Board ----------
pcb_off = 2.5;
pcb_t   = 1.6;
z_pcb   = floor_t + 12;
function P(p) = [MX(p[0]), p[1] + pcb_off];
pcb_holes   = [[66,57], [146,57], [194,57], [258,54], [258,110]];   // KiCad
pcb_support = [44,110];

// ---------- Inside ----------
side_margin = 13;                                // margin beside the outer pumps
in_x0 = pump_x[0] - 21 - side_margin;
in_x1 = pump_x[4] + 21 + side_margin;
in_y1 = 125 + pcb_off + 3;                       // 3 mm gap: the DIN rail nuts sit behind the board
z_top = 47.5;

// ---------- Jack slot (KiCad x 125.7..166) ----------
slot_x0 = MX(166);  slot_x1 = MX(125.7);
slot_y0 = 100 + pcb_off;
slot_wall = 1.5;
cable_gap_z = z_pcb + pcb_t + 9;

// ---------- Cross wall over the jacks (lid part) ----------
// Front of the jacks: board cut-out at KiCad y 100 -> Y slot_y0.
// Wall 1 mm behind it in case the RJ45 jack sticks out a little. Generous cut-outs;
// check the position against the assembled module.
cw_y   = slot_y0 + 1;   cw_t = 1.5;
cw_split_z = 27;        // split: below it the wall of the base, above it the wall on the lid
eth_x  = MX(139.2);  eth_w = 19;  eth_z0 = 26.5;  eth_z1 = 42.5;   // RJ45 on the USR-ES1
dc_x   = MX(158.7);  dc_w  = 12;  dc_z1  = 30.5;                    // barrel jack (open to the bottom)

// ---------- Reset/setup button: pin hole with guide tube and plug ----------
btn_hole_d = 3;          // for a paper clip
btn_cb_d   = 7;  btn_cb_h = 1.2;    // counterbore on top for the plug
btn_tube_d = 6.5;        // outer diameter of the guide tube
btn_tube_z = 30;         // tube ends about 3 mm above the button plunger (board z 16.6 + button about 10.4)
plug_fit   = 0.15;       // oversize of the plug (press fit in TPU)

// ---------- Status light in the front, right of pump 5 ----------
led_x = pump_x[4] + 21 + side_margin / 2;
led_z = 25;
led_d = 8;

// ---------- Threads / screws ----------
ins_d = 4.0;  ins_h = 6.5;  scr_d = 3.4;   // heat-set insert M3x5.7 (e.g. ruthex RX-M3x5.7): hole 4.0 mm, depth >= 6 mm
csk_d = 6.6;                                     // M3 countersunk head (90 deg)

// ---------- Lid screws: columns ----------
col_w = 7;
cx0 = in_x0 + col_w/2;  cx1 = in_x1 - col_w/2;
// [x, y, bottom]; front right only above the status light
cols = [[cx0, col_w/2, 0], [cx1, col_w/2, 31],
        [(pump_x[1] + pump_x[2]) / 2, col_w/2, 0],   // between pump 2 and 3
        [(pump_x[2] + pump_x[3]) / 2, col_w/2, 0],   // between pump 3 and 4
        [cx0, 67, 0],      [cx1, 67, 0],
        [cx0, in_y1 - col_w/2, 0], [cx1, in_y1 - col_w/2, 0],
        // back centre: beside the jack slot, hanging from the back wall above the board
        [slot_x1 + slot_wall + 0.5 + col_w/2, in_y1 - col_w/2, 30]];

// ---------- DIN rail clips (35 x 47 mm, holes 3.2 mm, 20 mm apart) ----------
// Fixed with M3 and a nut inside; the clip ends flush with the top of the enclosure
din_x = [85, 225];           // clip centres, left and right of the jack slot
din_z = [12, 32];            // holes 20 mm apart
din_d = 3.4;

// ---------- Floor ----------
drain_y  = 8;                                    // right behind the front, under each motor
drain_d_out = 3;  drain_d_in = 8;
feet = [[in_x0 + 8, 30], [in_x1 - 8, 30], [in_x0 + 8, in_y1 - 15], [in_x1 - 8, in_y1 - 15]];
foot_d = 10.5;  foot_h = 1;

// =====================================================================
ox0 = in_x0 - wall;   ox1 = in_x1 + wall;
oy0 = -front_t;       oy1 = in_y1 + wall;
module box(x0,y0,z0,x1,y1,z1) { translate([x0,y0,z0]) cube([x1-x0, y1-y0, z1-z0]); }
module rbox(x0,y0,z0,x1,y1,z1,r) {
    hull() for (x = [x0 + r, x1 - r], y = [y0 + r, y1 - r])
        translate([x, y, z0]) cylinder(r = r, h = z1 - z0);
}

module pump_cutout(px) {
    translate([px, 0.5, pump_z]) rotate([90,0,0]) {
        // mirrored: drawing from the back -> front view
        rotate([0,0,-pump_cut_rot]) translate([-pump_cut/2, -pump_cut/2, 0]) cube([pump_cut, pump_cut, front_t + 1]);
        for (h = pump_holes) translate([-h[0], h[1], 0]) cylinder(d = pump_hole_d, h = front_t + 1);
    }
}

module shell() {
    hull() {
        rbox(ox0 + chamfer, oy0 + chamfer, 0, ox1 - chamfer, oy1 - chamfer, 0.01, corner_r - chamfer);
        rbox(ox0, oy0, chamfer, ox1, oy1, z_top, corner_r);
    }
}

module closure_lower() {
    // lower closing wall (base), right behind the board edge; cut-out for the barrel jack open to the top
    difference() {
        box(slot_x0 - 0.01, cw_y, 0, slot_x1 + 0.01, cw_y + cw_t, cw_split_z);
        box(dc_x - dc_w/2, cw_y - 1, z_pcb + pcb_t - 0.5, dc_x + dc_w/2, cw_y + cw_t + 1, cw_split_z + 1);
    }
}

module base() {
  union() {
    closure_lower();
    difference() {
        union() {
            difference() { shell(); box(in_x0, 0, floor_t, in_x1, in_y1, z_top + 1); }
            // board domes
            for (h = pcb_holes) translate(concat(P(h), 0)) cylinder(d = 8, h = z_pcb);
            translate(concat(P(pcb_support), 0)) cylinder(d = 6, h = z_pcb);
            // slot: lower walls
            box(slot_x0 - slot_wall, slot_y0 - slot_wall, 0, slot_x1 + slot_wall, slot_y0, z_pcb);
            box(slot_x0 - slot_wall, slot_y0, 0, slot_x0, oy1, z_pcb);
            box(slot_x1, slot_y0, 0, slot_x1 + slot_wall, oy1, z_pcb);
            // screw columns for the lid
            for (c = cols)
                box(c[0] - col_w/2, c[1] - col_w/2, c[2], c[0] + col_w/2, c[1] + col_w/2, z_top);
        }
        // pumps
        for (px = pump_x) pump_cutout(px);
        // status light
        translate([led_x, 0.5, led_z]) rotate([90,0,0]) cylinder(d = led_d, h = front_t + 1);
        // pump numbers below the heads (left seen from the front = 1)
        for (i = [0:4]) translate([pump_x[i], oy0 + 0.6, 1.2]) rotate([90,0,0])
            linear_extrude(1) text(str(i + 1), size = pump_num_size, halign = "center", font = "Liberation Sans:style=Bold");
        // jack slot, open to the back and to the bottom
        box(slot_x0, slot_y0, -1, slot_x1, oy1 + 1, z_top + 1);
        // inserts: board, lid
        for (h = pcb_holes) translate(concat(P(h), z_pcb - ins_h)) cylinder(d = ins_d, h = ins_h + 1);
        for (c = cols) translate([c[0], c[1], z_top - ins_h]) cylinder(d = ins_d, h = ins_h + 1);
        // DIN rail clips: through holes
        for (x = din_x, z = din_z) translate([x, in_y1 - 1, z]) rotate([-90,0,0]) cylinder(d = din_d, h = wall + 2);
        // drain funnel behind each pump
        for (px = pump_x) translate([px, drain_y, -0.01])
            cylinder(d1 = drain_d_out, d2 = drain_d_in, h = floor_t + 0.02);
        // rubber feet
        for (f = feet) translate([f[0], f[1], -0.01]) cylinder(d = foot_d, h = foot_h);
    }
}
  }

module lid() {
    zb = z_pcb + pcb_t + 0.5;
    difference() {
        union() {
            hull() {
                rbox(ox0, oy0, z_top, ox1, oy1, z_top + lid_t - chamfer, corner_r);
                rbox(ox0 + chamfer, oy0 + chamfer, z_top + lid_t - 0.01, ox1 - chamfer, oy1 - chamfer, z_top + lid_t, corner_r - chamfer);
            }
            // upper slot walls; cable passage on the left side of the slot
            box(slot_x1, slot_y0, zb, slot_x1 + slot_wall, oy1, z_top);
            box(slot_x0 - slot_wall, slot_y0, zb, slot_x0, slot_y0 + 2, z_top);
            box(slot_x0 - slot_wall, slot_y0 + 2, cable_gap_z, slot_x0, oy1 - 3, z_top);
            box(slot_x0 - slot_wall, oy1 - 3, zb, slot_x0, oy1, z_top);
            // guide tube for the button
            translate([P([112,114])[0], P([112,114])[1], btn_tube_z]) cylinder(d = btn_tube_d, h = z_top - btn_tube_z + 0.01);
            // upper closing wall (lid), cut-outs open to the bottom
            difference() {
                box(slot_x0 - slot_wall, cw_y, cw_split_z, slot_x1 + slot_wall, cw_y + cw_t, z_top);
                box(eth_x - eth_w/2, cw_y - 1, cw_split_z - 1, eth_x + eth_w/2, cw_y + cw_t + 1, eth_z1);
                box(dc_x - dc_w/2, cw_y - 1, cw_split_z - 1, dc_x + dc_w/2, cw_y + cw_t + 1, dc_z1);
            }
        }
        // countersunk screws
        for (c = cols) {
            translate([c[0], c[1], z_top - 1]) cylinder(d = scr_d, h = lid_t + 2);
            translate([c[0], c[1], z_top + lid_t - (csk_d - scr_d)/2]) cylinder(d1 = scr_d, d2 = csk_d, h = (csk_d - scr_d)/2 + 0.01);
        }
        // button: pin hole through lid and guide tube, counterbore for the plug
        translate([P([112,114])[0], P([112,114])[1], btn_tube_z - 1]) cylinder(d = btn_hole_d, h = z_top + lid_t - btn_tube_z + 2);
        translate([P([112,114])[0], P([112,114])[1], z_top + lid_t - btn_cb_h]) cylinder(d = btn_cb_d, h = btn_cb_h + 1);
    }
}

// ---------- Preview ----------
module pcb_preview() {
    color("green", 0.6) difference() {
        box(MX(262), 50 + pcb_off, z_pcb, MX(38), 125 + pcb_off, z_pcb + pcb_t);
        box(slot_x0, slot_y0, z_pcb - 1, slot_x1, 126 + pcb_off, z_pcb + pcb_t + 1);
    }
}
module pumps_preview() {
    for (px = pump_x) color("silver", 0.7) {
        translate([px - 21, 0, pump_z - 21]) cube([42, motor_len, 41.9]);
        translate([px - 21, -front_t - 33.9, pump_z - 21]) cube([42, 33.9, 41.9]);
    }
}

module plug() {   // plug, TPU
    cylinder(d = btn_cb_d - 0.2, h = btn_cb_h);                         // head, flush in the counterbore
    translate([0,0,-1.2]) cylinder(d = btn_hole_d + plug_fit, h = 1.21); // pin, press fit
    translate([0,0,-1.2]) cylinder(d1 = btn_hole_d - 0.6, d2 = btn_hole_d + plug_fit, h = 0.4);
}

if (part == "base") base();
else if (part == "plug") translate([0,0,btn_cb_h]) rotate([180,0,0]) plug();
else if (part == "lid") translate([0, 0, -z_top]) lid();
else if (part == "assembly") {
    color("dimgray") base();
    color("gray", 0.5) translate([0, 0, 25]) lid();
    pcb_preview();
    pumps_preview();
}
