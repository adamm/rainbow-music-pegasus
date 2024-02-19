$fn = 100;
pcb_w = 138;
pcb_l = 30.5;
pcb_h = 1.6;

rgb_x = 21.85;
rgb_y = 4.5;
rgb_z = 1;
rgb_l = 9;
rgb_w = 10;
rgb_h = 2;

module microphone() {
    translate([8.0, pcb_l-3.7, 0])
        rotate([0,90,180])
            cylinder(13, 5, 5, false);
}

module switch() {
    translate([-10, 10.5, pcb_h/2-0.1])
        cube([13.4, 9, 4], false);
}

module usb() {
    translate([-10, 1, 0])
        cube([15, 8, 3], false);
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

module pcb() {
    translate([0, 0, -pcb_h/2])
    difference() {
        linear_extrude(height=pcb_h) {
            polygon(points=[[0,0], [0, 22], [3, 22], [3, pcb_l], [7, pcb_l],
            [7, pcb_l-2.5], [41, pcb_l-2.5], [41, rgb_l], [pcb_w, rgb_l],
            [pcb_w, 0]
            ]);
        }
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
        holes();
    }
}

union() {
     microphone();
    switch();
    usb();
    pcb();
}
