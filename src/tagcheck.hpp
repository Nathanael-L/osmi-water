/*
 * tagcheck.hpp
 *
 *  Created on: Jul 6, 2015
 *      Author: nathanael
 */

#ifndef TAGCHECK_HPP_
#define TAGCHECK_HPP_

#include <string>
#include <osmium/osm/tag.hpp>
#include <osmium/tags/filter.hpp>
#include <osmium/tags/tags_filter.hpp>


using namespace std;

class TagCheck {

    static const std::string get_waterway_type(const char *raw_type) {
        if (!raw_type) {
            return "";
        }
        if ((strcmp(raw_type, "river")) && (strcmp(raw_type, "stream")) 
                && (strcmp(raw_type, "drain")) && (strcmp(raw_type, "brook"))
                && (strcmp(raw_type, "canal")) && (strcmp(raw_type,"ditch"))
                && (strcmp(raw_type, "riverbank"))) {
            return "other";
        } else {
            return raw_type;
        }
    }

public:

    static bool is_waterway(const osmium::OSMObject &osm_object,
                            bool is_relation) {
        const char* type = osm_object.get_value_by_key("type");
        if ((type) && (!strcmp(type, "multipolygon"))) {
            return false;
        }
        const char *waterway = osm_object.get_value_by_key("waterway");
        if ((waterway) && (!strcmp(waterway, "riverbank"))) {
            return false;
        }
        if (is_relation) {
            if ((type) && (!strcmp(type, "waterway"))) {
                return true;
            }
        }
        if (waterway) {
            return true;
        }
        if (!is_relation) {
            const char *natural = osm_object.get_value_by_key("natural");
            if ((natural) && (!strcmp(natural, "coastline"))) {
                return true;
            }
        }
        return false;
    }

    static osmium::TagsFilter build_waterpolygon_filter() {
        osmium::TagsFilter filter{false};
        filter.add_rule(true, osmium::TagMatcher{"natural", "water"});
        filter.add_rule(true, osmium::TagMatcher{"waterway"});
        filter.add_rule(true, osmium::TagMatcher{"landuse", "reservoir"});
        filter.add_rule(true, osmium::TagMatcher{"landuse", "basin"});
        return filter;
    }

    static bool has_waterway_tag(const osmium::OSMObject &osm_object) {
        const char *waterway = osm_object.get_value_by_key("waterway");
        return static_cast<bool>(waterway);
    }

    static bool is_way_to_analyse(const osmium::OSMObject &osm_object) {
        const char *waterway = osm_object.get_value_by_key("waterway");
        if (waterway) {
            return true;
        }
        //if ((waterway) && (!strcmp(waterway, "riverbank"))) {
        //    return true;
        //}
        const char *natural = osm_object.get_value_by_key("natural");
        if ((natural) && (!strcmp(natural, "coastline"))) {
            return true;
        }
        if ((natural) && (!strcmp(natural, "water"))) {
            return true;
        }
        const char* landuse = osm_object.get_value_by_key("landuse");
        if ((landuse) && (!strcmp(landuse, "reservoir"))) {
            return true;
        }
        if ((landuse) && (!strcmp(landuse, "basin"))) {
            return true;
        }
        return false;
    }

    static bool is_area_to_analyse(const osmium::OSMObject &osm_object) {
        const char *waterway = osm_object.get_value_by_key("waterway");
        if ((waterway)
                && ((!strcmp(waterway, "river"))
                 || (!strcmp(waterway, "drain"))
                 || (!strcmp(waterway, "stream"))
                 || (!strcmp(waterway, "canal"))
                 || (!strcmp(waterway, "ditch"))
                 || (!strcmp(waterway, "riverbank")))) {
            return false;
        }
        const char *water = osm_object.get_value_by_key("water");
        if ((water)
                && ((!strcmp(water, "river"))
                 || (!strcmp(water, "drain"))
                 || (!strcmp(water, "stream"))
                 || (!strcmp(water, "canal"))
                 || (!strcmp(water, "ditch"))
                 || (!strcmp(water, "riverbank")))) {
            return false;
        }
        return true;
    }

    static bool is_riverbank_or_coastline(const osmium::OSMObject &osm_object) {
        const char *waterway = osm_object.get_value_by_key("waterway");
        if ((waterway) && (!strcmp(waterway, "riverbank"))) {
            return true;
        }
        const char *natural = osm_object.get_value_by_key("natural");
        if ((natural) && (!strcmp(natural, "coastline"))) {
            return true;
        }
        return false;
    }

    static bool is_water_area(const osmium::OSMObject &osm_object) {
        const char *natural = osm_object.get_value_by_key("natural");
        if ((natural) && (!strcmp(natural, "water"))) {
            return true;
        }
        const char *landuse = osm_object.get_value_by_key("landuse");
        if ((landuse) && ((!strcmp(landuse, "reservoir")) ||
                          (!strcmp(landuse, "basin")))){
            return true;
        }
        if (osm_object.get_value_by_key("waterway")) {
            return true;
        }
        return false;
    }

    static char get_waterway_category(const char *type) {
        if ((!strcmp(type, "drain")) || (!strcmp(type, "brook"))
                || (!strcmp(type, "ditch"))) {
            return 'A';
        } else if (!strcmp(type,"stream")) {
            return 'B';
        } else if (!strcmp(type,"river")) {
            return 'C';
        } else {
            return '?';
        }
    }

    static const std::string get_polygon_type(const osmium::Area &area) {
        const char *natural = area.get_value_by_key("natural");
        if ((natural) && (!strcmp(natural, "coastline"))) {
            return "coastline";
        }
        if (get_waterway_type(area.get_value_by_key("waterway")).empty()) {
            return area.get_value_by_key("landuse", "");
        }
        return "";
    }

    static const std::string get_way_type(const osmium::OSMObject &osm_object) {
        const char *waterway = osm_object.get_value_by_key("waterway");
        std::string type = get_waterway_type(waterway);
        if (type.empty()) {
            const char *natural = osm_object.get_value_by_key("natural");
            if ((natural) && (!strcmp(natural, "coastline"))) {
                return "coastline";
            } else {
                return "";
            }
        }
        return type;
    }

    static const char *get_width(const osmium::OSMObject &osm_object) {
        const char *width = osm_object.get_value_by_key("width");
        if (width) {
            return width;
        }
        width = osm_object.get_value_by_key("est_width");
        if (width) {
            return width;
        }
        return width;
    }

    static const std::string get_construction(const osmium::OSMObject &osm_object) {
        if (osm_object.get_value_by_key("bridge")) {
            return "bridge";
        }
        if (osm_object.get_value_by_key("tunnel")) {
            return "tunnel";
        }
        return "";
    }
};

#endif /* TAGCHECK_HPP_ */
