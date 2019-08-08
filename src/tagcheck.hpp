/*
 * tagcheck.hpp
 *
 *  Created on: Jul 6, 2015
 *      Author: nathanael
 */

#ifndef TAGCHECK_HPP_
#define TAGCHECK_HPP_

#include <osmium/osm/tag.hpp>
#include <osmium/tags/filter.hpp>
#include <osmium/tags/tags_filter.hpp>


using namespace std;

class TagCheck {

    static const char *get_waterway_type(const char *raw_type) {
        if (!raw_type) {
            return nullptr;
        }
        if ((strcmp(raw_type, "river")) && (strcmp(raw_type, "stream")) 
                && (strcmp(raw_type, "drain")) && (strcmp(raw_type, "brook"))
                && (strcmp(raw_type, "canal")) && (strcmp(raw_type,"ditch"))
                && (strcmp(raw_type, "riverbank"))) {
            return strdup("other\0");
        } else {
            return strdup(raw_type);
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

    static const char *get_polygon_type(const osmium::Area &area) {
        const char *type;
        const char *natural = area.get_value_by_key("natural");
        if ((natural) && (!strcmp(natural, "coastline"))) {
            type = "coastline";
        } else {
            type = get_waterway_type(area.get_value_by_key("waterway"));
            //if (!type) {
            //    type = area.get_value_by_key("water");
            //}
            if (!type) {
                type = area.get_value_by_key("landuse");
            }
            if (!type) type = "";
        }
        return type;
    }

    static const char *get_way_type(const osmium::OSMObject &osm_object) {
        const char *type;
        const char *waterway = osm_object.get_value_by_key("waterway");
        type = get_waterway_type(waterway);
        if (!type) {
            const char *natural = osm_object.get_value_by_key("natural");
            if ((natural) && (!strcmp(natural, "coastline"))) {
                type = "coastline";
            }
        }
        if (!type) type = "";
        return type;
    }

    static const char *get_name(const osmium::OSMObject &osm_object) {
        const char *name = osm_object.get_value_by_key("name");
        if (!name) name = "";
        return name;
    }

    static const char *get_width(const osmium::OSMObject &osm_object) {
        const char *width;
        if (osm_object.get_value_by_key("width")) {
            width = osm_object.get_value_by_key("width");
        } else if (osm_object.get_value_by_key("est_width")) {
            width = osm_object.get_value_by_key("est_width");
        } else {
            width = "";
        }
        return width;
    }

    static const char *get_construction(const osmium::OSMObject &osm_object) {
        const char *construction;
        if (osm_object.get_value_by_key("bridge")) {
            construction = "bridge";
        } else if (osm_object.get_value_by_key("tunnel")) {
            construction = "tunnel";
        } else {
            construction = "";
        }
        return construction;
    }
};

#endif /* TAGCHECK_HPP_ */
