$fn = 100;
pcb_w = 151;
pcb_l = 30.5;
pcb_h = 2.2;

rgb_x = 34.8525;
rgb_y = 4.5;
rgb_z = 1;
rgb_l = 9;
rgb_w = 10;
rgb_h = 2;

module microphone() {
    translate([20.0, pcb_l-3.0, 0.8])
        rotate([0,90,180])
            cylinder(27, 5.5, 5.5, false);
}

module switch() {
    translate([-4, 10.5, pcb_h-0.1])
        cube([13, 10.5, 5], false);
}

module usb() {
    translate([-4, 0.6, pcb_h-0.5])
        cube([13, 8.75, 3.5], false);
}

module led(x) {
    translate([rgb_x+x, rgb_y, rgb_z]) {
        cylinder(3, 2, 2, true);
    }
}

module holes(x) {
    translate([10.3333, pcb_l-6, -1]) {
        cylinder(4, 1.2, 1.2);
    }
    translate([37.8333, pcb_l-6, -1]) {
        cylinder(4, 1.2, 1.2);
    }
}

module pcb(draw_holes=true) {
    //translate([0, 0, -pcb_h/2])
    difference() {
        linear_extrude(height=pcb_h) {
            // Add 0.25mm to all ends to better align with
            // 3d printer tolerance
            offset(delta=+0.25) {
                polygon(points=[[0,0], [0, 10], [3, 10], [3, 22], [6,22], [6, pcb_l], [10, pcb_l],
                [10, pcb_l-2.5], [41.5, pcb_l-2.5], [41.5, rgb_l], [pcb_w, rgb_l],
                [pcb_w, 0]
                ]);
                translate([41.5, 9, 0]) {
                    intersection() {
                        square(19);
                        circle(r=19);
                    }
                }
            }
        }
        if (draw_holes) {
            led(0);
            led(10);
            led(20);
            led(30);
            led(40);
            led(50);
            led(60);
            led(70);
            led(80);
            led(90);
            led(100);
            led(110);
        }
    }
}

rotate([90, 0, 90]) {
    union() {
        //microphone();
        //switch();
        //usb();
        pcb(true);
    }
}
