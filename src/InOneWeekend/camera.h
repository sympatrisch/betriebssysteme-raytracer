// Beginn Include Guard
// Verhindert, dass camera.h mehrmals eingebunden wird
#ifndef CAMERA_H
#define CAMERA_H
//==============================================================================================
// Originally written in 2016 by Peter Shirley <ptrshrl@gmail.com>
//
// To the extent possible under law, the author(s) have dedicated all copyright and related and
// neighboring rights to this software to the public domain worldwide. This software is
// distributed without any warranty.
//
// You should have received a copy (see file COPYING.txt) of the CC0 Public Domain Dedication
// along with this software. If not, see <http://creativecommons.org/publicdomain/zero/1.0/>.
//==============================================================================================

#include "hittable.h"
#include "material.h"

#include <sys/mman.h>   // für mmap(), PROT_READ, PROT_WRITE, MAP_SHARED, MAP_ANONYMOUS
#include <unistd.h>     // für fork(), getpid(), sleep()
#include <sys/wait.h>   // für wait()


// Was die Kamera "sieht" (Standardwerte):
class camera {
  public:
    double aspect_ratio      = 1.0;  // Breite/Höhe des Bildes (z.B. 16:9)
    int    image_width       = 100;  // Breite in Pixeln
    int    samples_per_pixel = 10;   // Anzahl der Strahlen pro Pixel (Anti-Aliasing)
    int    max_depth         = 10;   // Max. Bounces von Lichtstrahlen (für Reflexion usw.)

    double vfov     = 90;              // Vertikaler Blickwinkel (field of view)
    point3 lookfrom = point3(0,0,0);   // Kamera-Position
    point3 lookat   = point3(0,0,-1);  // Blickziel
    vec3   vup      = vec3(0,1,0);     // "Oben"-Richtung in der Welt

    double defocus_angle = 0;  // Unschärfe durch Tiefenschärfe (für realistische Kamera)
    double focus_dist = 10;    // Fokusabstand

    // 1
    // Zeilennummer j, Zeilenbreite image_width, Welt world, Zeiger auf rendered_image-Array
    void renderLine(int j, int image_width,const hittable& world, color* rendered_image) {
        for (int i = 0; i < image_width; i++) {
            color pixel_color(0,0,0);
            for (int sample = 0; sample < samples_per_pixel; sample++) {
                ray r = get_ray(i, j);
                pixel_color += ray_color(r, max_depth, world);
            }
            rendered_image[j * image_width + i] = pixel_samples_scale * pixel_color; // 2
        }
    }

    // Hauptfunktion, die das Bild berechnet
    void render(const hittable& world) {
        initialize(); // Kamera und Parameter berechnen

        // 2
        // Gemeinsamen Bildspeicher anlegen
        int image_size_in_bytes = sizeof(color) * image_width * image_height;
        color* rendered_image = (color*) mmap(nullptr, image_size_in_bytes,
            PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);

        // Anzahl Kindprozesse (kannst du als Argument übergeben oder fix setzen)
        int n = 1; // Ich habe 16 Kerne
        int rows_per_process = image_height / n;

        // 3
        // n Kindprozesse erzeugen
        for (int p = 0; p < n; p++) {
            if (fork() == 0) {
                int start_row = p * rows_per_process;
                int end_row = (p == n-1) ? image_height : (p + 1) * rows_per_process;

                for (int j = start_row; j < end_row; j++) {
                    renderLine(j, image_width, world, rendered_image);
                }
                exit(0); // Kindprozess beendet sich sauber
            }
        }

        // 4
        // Elternprozess wartet auf alle Kinder
        for (int i = 0; i < n; i++) {
            wait(NULL);
        }

        // 4
        // Elternprozess gibt Bild aus
        std::cout << "P3\n" << image_width << ' ' << image_height << "\n255\n";

        for (int j = 0; j < image_height; j++) {
            for (int i = 0; i < image_width; i++) {
                write_color(std::cout, rendered_image[j * image_width + i]);
            }
        }
    }


  private:
    int    image_height;         // Rendered image height
    double pixel_samples_scale;  // Color scale factor for a sum of pixel samples
    point3 center;               // Camera center
    point3 pixel00_loc;          // Location of pixel 0, 0
    vec3   pixel_delta_u;        // Offset to pixel to the right
    vec3   pixel_delta_v;        // Offset to pixel below
    vec3   u, v, w;              // Camera frame basis vectors
    vec3   defocus_disk_u;       // Defocus disk horizontal radius
    vec3   defocus_disk_v;       // Defocus disk vertical radius

    void initialize() {
        image_height = int(image_width / aspect_ratio); // Bildhöhe berechnen
        image_height = (image_height < 1) ? 1 : image_height;

        pixel_samples_scale = 1.0 / samples_per_pixel; // Farbskalierung vorbereiten

        center = lookfrom; // Kameraposition setzen

        // Bildfenstergröße (Viewport) berechnen
        auto theta = degrees_to_radians(vfov); // Vertikaler Blickwinkel in Grad
        auto h = std::tan(theta/2); // Höhe vom Bild
        auto viewport_height = 2 * h * focus_dist; // Wie weit weg der virtuelle Bildschirm ist
        auto viewport_width = viewport_height * (double(image_width)/image_height);

        // Kamera-Koordinatensystem aufspannen
        w = unit_vector(lookfrom - lookat); // Blickrichtung (nach hinten)
        u = unit_vector(cross(vup, w)); // rechts
        v = cross(w, u); // oben

        // Vektoren über den Bildrand
        vec3 viewport_u = viewport_width * u;    // Vektor von ganz links nach rechts auf dem Bildschirm
        vec3 viewport_v = viewport_height * -v;  // Vektor von oben nach unten

        // Pixelgröße in Weltkoordinaten
        pixel_delta_u = viewport_u / image_width;
        pixel_delta_v = viewport_v / image_height;

        // Pixel (0, 0) berechnen (oben links).
        auto viewport_upper_left = center - (focus_dist * w) - viewport_u/2 - viewport_v/2;
        pixel00_loc = viewport_upper_left + 0.5 * (pixel_delta_u + pixel_delta_v);

        // Defokus-Berechnung (Unschärfeeffekt)
        auto defocus_radius = focus_dist * std::tan(degrees_to_radians(defocus_angle / 2));
        defocus_disk_u = u * defocus_radius;
        defocus_disk_v = v * defocus_radius;
    }
    // Erzeugt einen Strahl (ray) für den Pixel (i, j) auf dem Bildschirm
    ray get_ray(int i, int j) const {
        // Construct a camera ray originating from the defocus disk and directed at a randomly
        // sampled point around the pixel location i, j.

        auto offset = sample_square();
        auto pixel_sample = pixel00_loc // berechnet den konkreten Ort im 3D-Raum
                          + ((i + offset.x()) * pixel_delta_u)
                          + ((j + offset.y()) * pixel_delta_v);
        // Woher der Strahl kommt
        auto ray_origin = (defocus_angle <= 0) ? center : defocus_disk_sample();
        auto ray_direction = pixel_sample - ray_origin; // Richtung des Strahls

        return ray(ray_origin, ray_direction); // Gibt den erzeugten Strahl zurück
    }
    // Zufallsverschiebung innerhalb eines Pixels
    vec3 sample_square() const {
        // Returns the vector to a random point in the [-.5,-.5]-[+.5,+.5] unit square.
        return vec3(random_double() - 0.5, random_double() - 0.5, 0);
    }
    // Zufälliger Punkt auf einer Scheibe (für Defokus)
    vec3 sample_disk(double radius) const {
        // Returns a random point in the unit (radius 0.5) disk centered at the origin.
        return radius * random_in_unit_disk();
    }
    // Erzeugt einen Startpunkt auf der Defokus-Scheibe
    point3 defocus_disk_sample() const {
        // Returns a random point in the camera defocus disk.
        auto p = random_in_unit_disk();
        return center + (p[0] * defocus_disk_u) + (p[1] * defocus_disk_v);
    }
    // Bestimmt die Farbe, die ein Strahl liefert.
    color ray_color(const ray &r, int depth, const hittable &world) const {
        // If we've exceeded the ray bounce limit, no more light is gathered.
        if (depth <= 0)
            return color(0,0,0);

        hit_record rec;

        if (world.hit(r, interval(0.001, infinity), rec)) {
            ray scattered;
            color attenuation;
            if (rec.mat->scatter(r, rec, attenuation, scattered))
                return attenuation * ray_color(scattered, depth-1, world);
            return color(0,0,0);
        }

        vec3 unit_direction = unit_vector(r.direction());
        auto a = 0.5*(unit_direction.y() + 1.0);
        return (1.0-a)*color(1.0, 1.0, 1.0) + a*color(0.5, 0.7, 1.0);
    }
};


#endif
