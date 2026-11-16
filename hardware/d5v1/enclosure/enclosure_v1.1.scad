// =====================================================================
//  LGAL Doser 5 - enclosure for board d5v1 v1.1
//  Based on the flat enclosure v15 (board v1.0). New for v1.1:
//   - 65 mm high; the pumps sit low, their tube nipples overhang the bottom by ~8 mm
//   - one U-shaped board upright behind the motors, components facing the back wall; the
//     motor cables run over its top edge to the pump plugs
//   - the board slides in from the top into guide slots and stands on blocks 4 mm above the
//     floor, the lid holds it down (no screws)
//   - barrel jack and RJ45 point down into a niche behind pump 3 that is open to the back
//     and to the bottom; the niche passes through the notch of the board
//   - reset/setup button pressed with a paper clip through the back wall
//   - DS3231 RTC module upside down in a pocket at the left side wall, held only at its corners
//   - printable: walls, ribs, lips and tabs are at least 2 mm thick
//   - status light in the front under pump 3, narrow side margins, no level sensors
//  Two parts: base (with front) + lid (smooth, no openings, stackable).
//  Material: PA12 (MJF), M3 heat-set inserts for the lid screws.
//
//  Coordinates (OpenSCAD, right-handed):
//   X: seen from the front, positive to the RIGHT; pump 1 is on the left
//   Y = 0 at the inside of the front panel, positive to the back
//   Z = 0 at the outside of the floor
//  Board (KiCad top view = view from the back): X = mb_xr - (x - 100), Z = mb_z1 - (y - 100)
//
//  Output:  part = "base" | "lid" | "plug" | "assembly"
//  Rendered with OpenSCAD 2026.10.11 (manifold backend), checked for collisions against the
//  board, its parts (also while sliding in) and the motors; not yet printed.
// =====================================================================

part = "assembly";
$fn = 48;

// ---------- General ----------
wall     = 2.5;
floor_t  = 3;
front_t  = 2.5;
lid_t    = 2.5;
corner_r = 4;           // radius of the vertical outside edges
chamfer  = 0.8;         // chamfer at the top edge of the lid and the bottom edge
height   = 65;          // outside, with the lid
z_top    = height - lid_t;

// ---------- Pumps (Kamoer KPHM100-ST, data sheet 2.3.1) ----------
pump_pitch   = 55;      // head incl. tube connectors 50 mm + 5 mm gap
pump_x       = [for (i = [-2:2]) 150 + i * pump_pitch];
pump_z       = 35;      // motor centre; flange bottom at z 14, nipple tips ~8 mm below the floor
motor_w      = 42;
motor_len    = 50.5;
pump_cut     = 30.3;
// The Kamoer drawing shows the mounting plate FROM THE BACK (motor side), so the hole
// pattern and the rotation are mirrored for the front view.
pump_cut_rot = 22;      // as in the drawing (from the back), counter-clockwise
pump_hole_d  = 3.5;
pump_holes   = [[-15.5, 15.5], [17.5, 8.5], [-17.5, -8.5], [15.5, -15.5]];  // as in the drawing (from the back)
pump_num_size = 2.5;    // engraved pump numbers under the heads

// ---------- Inside ----------
side_margin = 7.5;                               // margin beside the outer pumps
in_x0 = pump_x[0] - motor_w/2 - side_margin;
in_x1 = pump_x[4] + motor_w/2 + side_margin;

// ---------- Board d5v1 v1.1 (205 x 50 mm, KiCad outline x 100..305, y 100..150) ----------
// U shape: notch x 167.5..207.5 from y 127 to the bottom edge, centred behind pump 3.
pcb_t  = 1.6;
mb_xr  = 237.5;                // KiCad x 100 (right edge seen from the front)
mb_xl  = mb_xr - 205;          // KiCad x 305
mb_z0  = floor_t + 4;          // bottom edge on support blocks, 4 mm above the floor (water)
mb_z1  = mb_z0 + 50;           // top edge
mb_y   = motor_len + 6;        // solder side; 6 mm behind the motors for solder joints and cables
comp_h = 26;                   // components on the back side (drivers with heat sink 25 mm)
in_y1  = mb_y + pcb_t + comp_h + 1;
function MB(p) = [mb_xr - (p[0] - 100), mb_z1 - (p[1] - 100)];
notch_x0 = MB([207.5, 0])[0];  notch_x1 = MB([167.5, 0])[0];
notch_z1 = MB([0, 127])[1];

// ---------- Guide slots for the board (left and right edge) ----------
slot_d   = 2;                  // the board edge reaches 2 mm into the slot (2.5 mm free on the board)
slot_clr = 0.2;                // clearance at the edge and on each side of the board
lip_t    = 2;                  // lips in front of and behind the board
rib_t    = 2;                  // rib behind the board edge, joined to the back wall
lead_in  = 3;                  // chamfer at the top of the slots
// support blocks under the bottom edge (the slots have their bottom at the same height)
sup_x = [70, 105, 190, 215];
sup_l = 8;

// ---------- Lid: tabs that hold the board down (between the pump plugs) ----------
tab_x = [MB([136.5, 0])[0], MB([239.8, 0])[0], MB([272, 0])[0]];
tab_w = 6;

// ---------- Niche: open to the back and to the bottom, through the notch ----------
nw     = 2;                     // wall thickness of the niche
n_x0   = notch_x0 + 0.5 + nw;   // inside
n_x1   = notch_x1 - 0.5 - nw;
n_y0   = motor_len + 2.5;       // front of the niche (inside)
n_ztop = MB([0, 128.5])[1] - 0.6;  // top of the ceiling, 0.6 mm under the jack faces
n_z1   = n_ztop - nw;           // ceiling (inside)
// Cut-outs in the ceiling for the plugs; check them against the assembled board.
comp_y = mb_y + pcb_t;          // component side
dc_x0  = MB([204.6, 0])[0];  dc_x1 = MB([192.3, 0])[0];          // barrel jack J1 (axis at KiCad x 198.3)
dc_y0  = comp_y - 0.5;       dc_y1 = comp_y + 12;
eth_x0 = MB([188.7, 0])[0];  eth_x1 = MB([172.6, 0])[0];          // RJ45 on the USR-ES1 (J7/J8)
eth_y0 = comp_y + 9;         eth_y1 = in_y1 - 0.01;

// ---------- Reset/setup button: pin hole in the back wall with guide tube and plug ----------
btn      = MB([292, 143]);     // SW1
btn_h    = 10.4;               // button height above the board (Multimec 3FTH9)
btn_hole_d = 3;                // for a paper clip
btn_cb_d = 7;  btn_cb_h = 1.2; // counterbore outside for the plug
btn_tube_d = 7;                // 2 mm wall around the pin hole
btn_tube_y = comp_y + btn_h + 3;     // tube ends ~3 mm above the plunger
plug_fit = 0.15;               // oversize of the plug (press fit in TPU)

// ---------- RTC module (DS3231 + AT24C32, ZS-042 type, measured about 38.3 x 21.7 mm) ----------
// Stands upright in a pocket at the left side wall, battery holder towards the wall, the 6-pin
// angled header at the bottom: its pins and the plug of the cable to J10 point down. Both long
// edges carry parts in the middle (IC leads about 1 mm from the edge, the battery holder
// overhangs one edge by about 1 mm), so the module is held only at its four corners: it stands in
// two short channels at the bottom corners, and two forks on the lid catch the top corners. Two
// webs from the side wall close the pocket at the front and the back. Check on the first print.
rtc_w     = 22;                // board width (measured about 21.7)
rtc_l     = 38.5;              // board length (measured about 38.3)
rtc_t     = 1.6;
rtc_bat   = 8;                 // battery holder below the board (measured 8 mm at the spring)
rtc_bat_w = 23;                // battery holder width, overhangs one long edge by about 1 mm
rtc_chip  = 4.5;               // highest part on the chip side: the bent pins of the header
rtc_xb    = in_x0 + rtc_bat + 1;    // battery side of its board (1 mm air to the side wall)
rtc_xc    = rtc_xb + rtc_t;         // chip side of its board
rtc_yc    = 64.8;              // centre of its board (holder clear of the lid screw column)
rtc_z0    = z_top - 0.5 - rtc_l;    // stands on the channel floors, 0.5 mm under the lid
rtc_clr   = 0.3;               // clearance around the board
rtc_grip  = 1.5;               // channels and forks hold 1.5 mm of each corner
rtc_hold  = 5;                 // height of the channels and forks
rtc_xo    = rtc_xc + rtc_clr + 2;   // outside of the channels (2 mm lips)
rtc_cable_y = [61, 77];        // opening in the left guide rib for the cable to J10 (behind the board)
rtc_cable_h = 10;

// ---------- Status light in the front, under pump 3 ----------
led_x = pump_x[2];
led_z = 8;
led_d = 8;

// ---------- Screws ----------
ins_d = 4.0;  ins_h = 6.5;  scr_d = 3.4;   // heat-set insert M3x5.7 (e.g. ruthex RX-M3x5.7): hole 4.0 mm, depth >= 6 mm
csk_d = 6.6;                                // countersunk head M3 (90 degrees)

// ---------- Lid screws: 6 columns ----------
col_w = 7;
cx0 = in_x0 + col_w/2;  cx1 = in_x1 - col_w/2;
cols = [[cx0, col_w/2], [cx1, col_w/2],
        [(pump_x[1] + pump_x[2]) / 2, col_w/2],      // between pump 2 and 3
        [(pump_x[2] + pump_x[3]) / 2, col_w/2],      // between pump 3 and 4
        [cx0, in_y1 - col_w/2], [cx1, in_y1 - col_w/2]];

// ---------- DIN rail clip (35 x 47 mm, holes 3.2 mm, 20 mm apart) ----------
// M3 countersunk from the inside (flush with the inner wall, behind the components), nut on
// the clip; the clips sit symmetric to the niche and end flush with the top of the lid.
din_x = [150 - 60, 150 + 60];
din_z = [height - 38, height - 18];
din_d = 3.4;

// ---------- Floor ----------
drain_y  = 8;                                    // right behind the front, under each motor
drain_d_out = 3;  drain_d_in = 8;
// Rubber feet: the tube nipples overhang the bottom by ~8 mm, so feet of at least 10 mm,
// or the device stands at the edge of a shelf.
foot_d = 10.5;  foot_h = 1;
foot_in = 7.5;                                   // centre from the outside edges, in the corners
feet = [for (x = [in_x0 - wall + foot_in, in_x1 + wall - foot_in],
             y = [-front_t + foot_in, in_y1 + wall - foot_in]) [x, y]];

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

// guide slot for a board edge at X = xe; s = +1 when the board lies to the right of the edge
module guide(xe, s) {
    ya = mb_y - slot_clr - lip_t;   yb = mb_y + pcb_t + slot_clr + lip_t;
    difference() {
        union() {
            // rib from the front lip to the back wall
            box(min(xe - s * slot_clr, xe - s * (slot_clr + rib_t)), ya, 0.5,
                max(xe - s * slot_clr, xe - s * (slot_clr + rib_t)), in_y1 + 0.01, z_top);
            // lips
            box(min(xe - s * slot_clr, xe + s * slot_d), ya, 0.5,
                max(xe - s * slot_clr, xe + s * slot_d), yb, z_top);
        }
        // slot with a lead-in at the top
        box(min(xe - s * slot_clr, xe + s * (slot_d + 1)), mb_y - slot_clr, mb_z0,
            max(xe - s * slot_clr, xe + s * (slot_d + 1)), mb_y + pcb_t + slot_clr, z_top + 1);
        hull() {
            box(min(xe - s * slot_clr, xe + s * (slot_d + 1)), mb_y - slot_clr, z_top - lead_in,
                max(xe - s * slot_clr, xe + s * (slot_d + 1)), mb_y + pcb_t + slot_clr, z_top - lead_in + 0.01);
            box(min(xe - s * slot_clr, xe + s * (slot_d + 1)), ya + 0.4, z_top,
                max(xe - s * slot_clr, xe + s * (slot_d + 1)), yb - 0.4, z_top + 0.01);
        }
    }
}

// pocket for the RTC module at the left side wall: two webs, channels at the bottom corners
function rtc_eb(s) = rtc_yc - s * (rtc_bat_w/2 + 1);       // web face beside the battery holder
function rtc_ep(s) = rtc_yc - s * (rtc_w/2 + rtc_clr);     // web face at the board
module rtc_pocket() {
    for (s = [1, -1]) {
        eb = rtc_eb(s);  ep = rtc_ep(s);  eg = ep + s * (rtc_clr + rtc_grip);
        // webs: wider beside the battery holder, narrower at the board and the chip side
        // (the front one stops short of the motor of pump 1)
        box(in_x0 - 0.01, min(eb, eb - s * 2), 0.5, s == 1 ? pump_x[0] - motor_w/2 : rtc_xb + 0.5, max(eb, eb - s * 2), z_top);
        box(rtc_xb, min(ep, ep - s * 2), 0.5, rtc_xo, max(ep, ep - s * 2), z_top);
        // channel at the bottom corner, with a 45 degree support under it and a lead-in at the top
        difference() {
            union() {
                box(rtc_xb - rtc_clr - 2, min(eb, eg), rtc_z0 - 2, rtc_xo, max(eb, eg), rtc_z0 + rtc_hold);
                hull() {
                    box(rtc_xb - rtc_clr - 2, min(eb, eg), rtc_z0 - 2, rtc_xo, max(eb, eg), rtc_z0 - 1.99);
                    box(rtc_xb - rtc_clr - 2, min(eb, eb + s * 0.01), rtc_z0 - 2 - abs(eg - eb), rtc_xo, max(eb, eb + s * 0.01), rtc_z0 - 1.99);
                }
            }
            box(rtc_xb - rtc_clr, min(ep - s, eg + s), rtc_z0, rtc_xc + rtc_clr, max(ep - s, eg + s), z_top);
            hull() {
                box(rtc_xb - rtc_clr, min(ep - s, eg + s), rtc_z0 + rtc_hold - 1.5, rtc_xc + rtc_clr, max(ep - s, eg + s), rtc_z0 + rtc_hold - 1.49);
                box(rtc_xb - rtc_clr - 1, min(ep - s, eg + s), rtc_z0 + rtc_hold, rtc_xc + rtc_clr + 1, max(ep - s, eg + s), rtc_z0 + rtc_hold + 0.01);
            }
        }
    }
}
// forks on the lid that catch the top corners of the RTC module
module rtc_forks() {
    for (s = [1, -1]) {
        ep = rtc_ep(s) + s * 0.2;  eg = rtc_ep(s) + s * (rtc_clr + rtc_grip);
        difference() {
            box(rtc_xb - rtc_clr - 2, min(ep, eg), z_top - rtc_hold, rtc_xo, max(ep, eg), z_top + 0.01);
            box(rtc_xb - rtc_clr, min(ep - s, eg + s), z_top - rtc_hold - 1, rtc_xc + rtc_clr, max(ep - s, eg + s), z_top + 1);
            hull() {
                box(rtc_xb - rtc_clr, min(ep - s, eg + s), z_top - rtc_hold + 1.5, rtc_xc + rtc_clr, max(ep - s, eg + s), z_top - rtc_hold + 1.51);
                box(rtc_xb - rtc_clr - 1, min(ep - s, eg + s), z_top - rtc_hold - 0.01, rtc_xc + rtc_clr + 1, max(ep - s, eg + s), z_top - rtc_hold);
            }
        }
    }
}

module niche_walls() {
    // front, sides and ceiling; they pass through the notch of the board
    box(n_x0 - nw, n_y0 - nw, 0, n_x1 + nw, n_y0, n_ztop);
    box(n_x0 - nw, n_y0 - nw, 0, n_x0, oy1 - 0.5, n_ztop);
    box(n_x1, n_y0 - nw, 0, n_x1 + nw, oy1 - 0.5, n_ztop);
    difference() {
        box(n_x0 - nw, n_y0 - nw, n_z1, n_x1 + nw, in_y1, n_ztop);
        box(dc_x0, dc_y0, n_z1 - 1, dc_x1, dc_y1, n_ztop + 1);
        box(eth_x0, eth_y0, n_z1 - 1, eth_x1, eth_y1, n_ztop + 1);
    }
}

module base() {
    difference() {
        union() {
            difference() { shell(); box(in_x0, 0, floor_t, in_x1, in_y1, z_top + 1); }
            guide(mb_xl, 1);
            guide(mb_xr, -1);
            for (x = sup_x) box(x - sup_l/2, mb_y - 1, 0.5, x + sup_l/2, mb_y + pcb_t + 1, mb_z0);
            niche_walls();
            rtc_pocket();
            // guide tube for the button
            translate([btn[0], btn_tube_y, btn[1]]) rotate([-90,0,0]) cylinder(d = btn_tube_d, h = in_y1 - btn_tube_y + 0.01);
            // lid screw columns
            for (c = cols)
                box(c[0] - col_w/2, c[1] - col_w/2, 0, c[0] + col_w/2, c[1] + col_w/2, z_top);
        }
        // pumps
        for (px = pump_x) pump_cutout(px);
        // status light
        translate([led_x, 0.5, led_z]) rotate([90,0,0]) cylinder(d = led_d, h = front_t + 1);
        // pump numbers under the heads (1 = left seen from the front)
        for (i = [0:4]) translate([pump_x[i], oy0 + 0.6, 1.2]) rotate([90,0,0])
            linear_extrude(1) text(str(i + 1), size = pump_num_size, halign = "center", font = "Liberation Sans:style=Bold");
        // niche, open to the back and to the bottom
        box(n_x0, n_y0, -1, n_x1, oy1 + 1, n_z1);
        // button: pin hole through back wall and tube, counterbore for the plug
        translate([btn[0], btn_tube_y - 1, btn[1]]) rotate([-90,0,0]) cylinder(d = btn_hole_d, h = oy1 - btn_tube_y + 2);
        translate([btn[0], oy1 - btn_cb_h, btn[1]]) rotate([-90,0,0]) cylinder(d = btn_cb_d, h = btn_cb_h + 1);
        // inserts for the lid screws
        for (c = cols) translate([c[0], c[1], z_top - ins_h]) cylinder(d = ins_d, h = ins_h + 1);
        // DIN rail clip: M3 countersunk from the inside
        for (x = din_x, z = din_z) {
            translate([x, in_y1 - 1, z]) rotate([-90,0,0]) cylinder(d = din_d, h = wall + 2);
            translate([x, in_y1 - 0.01, z]) rotate([-90,0,0])
                cylinder(d1 = csk_d, d2 = scr_d, h = (csk_d - scr_d)/2);
        }
        // cable opening for the RTC cable at the bottom of the left guide rib, behind the board
        box(mb_xl - slot_clr - rib_t - 0.1, rtc_cable_y[0], floor_t, mb_xl - slot_clr + 0.01, rtc_cable_y[1], floor_t + rtc_cable_h);
        // drain funnel under each pump
        for (px = pump_x) translate([px, drain_y, -0.01])
            cylinder(d1 = drain_d_out, d2 = drain_d_in, h = floor_t + 0.02);
        // rubber feet
        for (f = feet) translate([f[0], f[1], -0.01]) cylinder(d = foot_d, h = foot_h);
    }
}

module lid() {
    difference() {
        union() {
            hull() {
                rbox(ox0, oy0, z_top, ox1, oy1, z_top + lid_t - chamfer, corner_r);
                rbox(ox0 + chamfer, oy0 + chamfer, z_top + lid_t - 0.01, ox1 - chamfer, oy1 - chamfer, z_top + lid_t, corner_r - chamfer);
            }
            // tabs on the top edge of the board, in front of its components
            for (x = tab_x) box(x - tab_w/2, mb_y - 2.5, mb_z1 + 0.2, x + tab_w/2, mb_y + pcb_t - 0.1, z_top + 0.01);
            rtc_forks();
        }
        // countersunk screws
        for (c = cols) {
            translate([c[0], c[1], z_top - 1]) cylinder(d = scr_d, h = lid_t + 2);
            translate([c[0], c[1], z_top + lid_t - (csk_d - scr_d)/2]) cylinder(d1 = scr_d, d2 = csk_d, h = (csk_d - scr_d)/2 + 0.01);
        }
    }
}

module plug() {   // plug for the button hole, TPU
    cylinder(d = btn_cb_d - 0.2, h = btn_cb_h);                         // head, flush in the counterbore
    translate([0,0,-1.2]) cylinder(d = btn_hole_d + plug_fit, h = 1.21); // pin, press fit
    translate([0,0,-1.2]) cylinder(d1 = btn_hole_d - 0.6, d2 = btn_hole_d + plug_fit, h = 0.4);
}

// ---------- Preview ----------
module board_plate() {
    difference() {
        box(mb_xl, mb_y, mb_z0, mb_xr, mb_y + pcb_t, mb_z1);
        box(notch_x0, mb_y - 1, mb_z0 - 1, notch_x1, mb_y + pcb_t + 1, notch_z1);
    }
}
module rtc_module() {   // board, battery holder (23 mm, centre 23.5 mm from the header end), header and plug
    box(rtc_xb, rtc_yc - rtc_w/2, rtc_z0, rtc_xc, rtc_yc + rtc_w/2, rtc_z0 + rtc_l);
    translate([rtc_xb - rtc_bat, rtc_yc + 0.5, rtc_z0 + 23.5]) rotate([0,90,0]) cylinder(d = rtc_bat_w, h = rtc_bat);
    box(rtc_xc, rtc_yc - rtc_w/2 + 2.2, rtc_z0, rtc_xc + rtc_chip, rtc_yc + rtc_w/2 - 3.8, rtc_z0 + 2.7);
    box(rtc_xc + 2.4, rtc_yc - rtc_w/2 + 2.2, rtc_z0 - 20, rtc_xc + 5, rtc_yc + rtc_w/2 - 3.8, rtc_z0);
}
module boards_preview() {
    color("green", 0.7) board_plate();
    // space of the components (25 mm towards the back wall), barrel jack and USR-ES1
    color("lightgreen", 0.15) difference() {
        box(mb_xl + 2.5, comp_y, mb_z0, mb_xr - 2.5, comp_y + comp_h, mb_z1);
        box(notch_x0, comp_y - 1, mb_z0 - 1, notch_x1, comp_y + comp_h + 1, notch_z1 + 1);
    }
    color("black", 0.8) box(MB([203, 0])[0], comp_y, MB([0, 128.5])[1], MB([193.5, 0])[0], comp_y + 11, MB([0, 114])[1]);
    color("silver", 0.8) box(MB([192.2, 0])[0], comp_y + 8.5, MB([0, 128.5])[1], MB([169.2, 0])[0], comp_y + 24, MB([0, 100.1])[1]);
    // RTC module
    color("blue", 0.7) rtc_module();
}
module pumps_preview() {
    for (px = pump_x) color("silver", 0.7) {
        translate([px - motor_w/2, 0, pump_z - motor_w/2]) cube([motor_w, motor_len, 41.9]);
        translate([px - motor_w/2, -front_t - 33.9, pump_z - motor_w/2]) cube([motor_w, 33.9, 41.9]);
        for (dx = [-12, 12]) translate([px + dx, -front_t - 17, pump_z - motor_w/2 - 22]) cylinder(d = 5, h = 22);
    }
}

if (part == "base") base();
else if (part == "plug") translate([0,0,btn_cb_h]) rotate([180,0,0]) plug();
else if (part == "lid") translate([0, 0, -z_top]) lid();
else if (part == "assembly") {
    color("dimgray") base();
    color("gray", 0.5) translate([0, 0, 25]) lid();
    boards_preview();
    pumps_preview();
}
