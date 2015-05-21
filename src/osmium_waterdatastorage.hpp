#include <geos/index/strtree/STRtree.h>
#include <geos/index/ItemVisitor.h>
using namespace std;

typedef osmium::index::map::Dummy<osmium::unsigned_object_id_type, osmium::Location> index_neg_type;
typedef osmium::index::map::SparseMemArray<osmium::unsigned_object_id_type, osmium::Location> index_pos_type;
typedef osmium::handler::NodeLocationsForWays<index_pos_type, index_neg_type> location_handler_type;

/***
 * Stores all important Data over the runtime and handle the database.
 */
class DataStorage {
    OGRDataSource* m_data_source;
    OGRLayer* m_layer_polygons;
    OGRLayer* m_layer_relations;
    OGRLayer* m_layer_ways;
    OGRLayer* m_layer_nodes;
    osmium::geom::OGRFactory<> m_ogr_factory;

    /***
     * Structure to remember the waterways according to the firstnodes and lastnodes of the waterways.
     *
     * Categories are:
     *  drain, brook, ditch = A
     *  stream              = B
     *  river               = C
     *  other, canal        = ?
     *  >> ignore canals, because can differ in floating direction and size
     */
    struct WaterWay {
        osmium::object_id_type firstnode;
        osmium::object_id_type lastnode;
        string name;
        char category;

        WaterWay(osmium::object_id_type first_node, osmium::object_id_type last_node, const char *name_, const char *type) {
            firstnode = first_node;
            lastnode = last_node;
            name = name_;
            if ((!strcmp(type,"drain"))||(!strcmp(type,"brook"))||(!strcmp(type,"ditch"))) {
                category = 'A';
            } else if (!strcmp(type,"stream")) {
                category = 'B';
            } else if (!strcmp(type,"river")) {
                category = 'C';
            } else {
                category = '?';
            }
        }
    };

    vector<WaterWay*> WaterWays;

    void init_db() {
CPLSetConfigOption("OGR_SQLITE_SYNCHRONOUS", "OFF");
        OGRRegisterAll();

        OGRSFDriver* driver = OGRSFDriverRegistrar::GetRegistrar()->GetDriverByName("SQLite");
        if (!driver) {
            cerr << "SQLite" << " driver not available.\n";
            exit(1);
        }

        CPLSetConfigOption("OGR_SQLITE_SYNCHRONOUS", "FALSE");
        const char* options[] = { "SPATIALITE=TRUE", nullptr };
        m_data_source = driver->CreateDataSource("/tmp/waterways.sqlite", const_cast<char**>(options));
        if (!m_data_source) {
            cerr << "Creation of output file failed.\n";
            exit(1);
        }

        OGRSpatialReference sparef;
        sparef.SetWellKnownGeogCS("WGS84");

        /*---- TABLE POLYGONS ----*/
        m_layer_polygons = m_data_source->CreateLayer("polygons", &sparef, wkbMultiPolygon, nullptr);
        if (!m_layer_polygons) {
            cerr << "Layer polygons creation failed.\n";
            exit(1);
        }

        OGRFieldDefn layer_polygons_field_way_id("way_id", OFTInteger);
        layer_polygons_field_way_id.SetWidth(12);
        if (m_layer_polygons->CreateField(&layer_polygons_field_way_id) != OGRERR_NONE) {
            cerr << "Creating way_id field failed.\n";
            exit(1);
        }

        OGRFieldDefn layer_polygons_field_relation_id("relation_id", OFTInteger);
        layer_polygons_field_relation_id.SetWidth(12);
        if (m_layer_polygons->CreateField(&layer_polygons_field_relation_id) != OGRERR_NONE) {
            cerr << "Creating relation_id field failed.\n";
            exit(1);
        }

        OGRFieldDefn layer_polygons_field_type("type", OFTString);
        layer_polygons_field_type.SetWidth(10);
        if (m_layer_polygons->CreateField(&layer_polygons_field_type) != OGRERR_NONE) {
            cerr << "Creating type field failed.\n";
            exit(1);
        }

        OGRFieldDefn layer_polygons_field_name("name", OFTString);
        layer_polygons_field_name.SetWidth(30);
        if (m_layer_polygons->CreateField(&layer_polygons_field_name) != OGRERR_NONE) {
            cerr << "Creating name field failed.\n";
            exit(1);
        }

        OGRFieldDefn layer_polygons_field_lastchange("lastchange", OFTString);
        layer_polygons_field_lastchange.SetWidth(20);
        if (m_layer_polygons->CreateField(&layer_polygons_field_lastchange) != OGRERR_NONE) {
            cerr << "Creating lastchange field failed.\n";
            exit(1);
        }

        OGRFieldDefn layer_polygons_field_error("error", OFTString);
        layer_polygons_field_error.SetWidth(6);
        if (m_layer_polygons->CreateField(&layer_polygons_field_error) != OGRERR_NONE) {
            cerr << "Creating error field failed.\n";
            exit(1);
        }

        int ogrerr = m_layer_polygons->StartTransaction();
        if (ogrerr != OGRERR_NONE) {
            cerr << "Creating polygons table failed.\n";
            cerr << "OGRERR: " << ogrerr << endl;
            exit(1);
        }


        /*---- TABLE RELATIONS ----*/
        m_layer_relations = m_data_source->CreateLayer("relations", &sparef, wkbMultiLineString, nullptr);
        if (!m_layer_relations) {
            cerr << "Layer relations creation failed.\n";
            exit(1);
        }

        OGRFieldDefn layer_relations_field_id("id", OFTInteger);
        layer_relations_field_id.SetWidth(12);
        if (m_layer_relations->CreateField(&layer_relations_field_id) != OGRERR_NONE) {
            cerr << "Creating id field in table realtions failed.\n";
            exit(1);
        }

        OGRFieldDefn layer_relations_field_type("type", OFTString);
        layer_relations_field_type.SetWidth(10);
        if (m_layer_relations->CreateField(&layer_relations_field_type) != OGRERR_NONE) {
            cerr << "Creating type field in table realtions failed.\n";
            exit(1);
        }

        OGRFieldDefn layer_relations_field_name("name", OFTString);
        layer_relations_field_type.SetWidth(30);
        if (m_layer_relations->CreateField(&layer_relations_field_name) != OGRERR_NONE) {
            cerr << "Creating name field in table realtions failed.\n";
            exit(1);
        }

        OGRFieldDefn layer_relations_field_lastchange("lastchange", OFTString);
        layer_relations_field_lastchange.SetWidth(20);
        if (m_layer_relations->CreateField(&layer_relations_field_lastchange) != OGRERR_NONE) {
            cerr << "Creating lastchange field in table realtions failed.\n";
            exit(1);
        }

        OGRFieldDefn layer_relations_field_nowaterway_error("nowaterway_error", OFTString);
        layer_relations_field_type.SetWidth(6);
        if (m_layer_relations->CreateField(&layer_relations_field_nowaterway_error) != OGRERR_NONE) {
            cerr << "Creating nowaterway_error field in table realtions failed.\n";
            exit(1);
        }

        OGRFieldDefn layer_relations_field_tagging_error("tagging_error", OFTString);
        layer_relations_field_type.SetWidth(6);
        if (m_layer_relations->CreateField(&layer_relations_field_tagging_error) != OGRERR_NONE) {
            cerr << "Creating tagging_error field in table realtions failed.\n";
            exit(1);
        }

        ogrerr = m_layer_relations->StartTransaction();
        if (ogrerr != OGRERR_NONE) {
            cerr << "Creating relations table failed.\n";
            cerr << "OGRERR: " << ogrerr << endl;
            exit(1);
        }

        /*---- TABLE WAYS ----*/
        m_layer_ways = m_data_source->CreateLayer("ways", &sparef, wkbLineString, nullptr);
        if (!m_layer_ways) {
            cerr << "Layer ways creation in table ways failed.\n";
            exit(1);
        }

        OGRFieldDefn layer_ways_field_id("id", OFTInteger);
        layer_ways_field_id.SetWidth(12);
        if (m_layer_ways->CreateField(&layer_ways_field_id) != OGRERR_NONE) {
            cerr << "Creating id field in table ways failed.\n";
            exit(1);
        }

        OGRFieldDefn layer_ways_field_type("type", OFTString);
        layer_ways_field_type.SetWidth(10);
        if (m_layer_ways->CreateField(&layer_ways_field_type) != OGRERR_NONE) {
            cerr << "Creating type field in table ways failed.\n";
            exit(1);
        }

        OGRFieldDefn layer_ways_field_name("name", OFTString);
        layer_ways_field_name.SetWidth(30);
        if (m_layer_ways->CreateField(&layer_ways_field_name) != OGRERR_NONE) {
            cerr << "Creating name field in table ways failed.\n";
            exit(1);
        }

        OGRFieldDefn layer_ways_field_firstnode("firstnode", OFTString);
        layer_ways_field_firstnode.SetWidth(11);
        if (m_layer_ways->CreateField(&layer_ways_field_firstnode) != OGRERR_NONE) {
            cerr << "Creating firstnode field in table ways failed.\n";
            exit(1);
        }

        OGRFieldDefn layer_ways_field_lastnode("lastnode", OFTString);
        layer_ways_field_lastnode.SetWidth(11);
        if (m_layer_ways->CreateField(&layer_ways_field_lastnode) != OGRERR_NONE) {
            cerr << "Creating lastnode field in table ways failed.\n";
            exit(1);
        }

        OGRFieldDefn layer_ways_field_relation("relation", OFTInteger);
        layer_ways_field_relation.SetWidth(10);
        if (m_layer_ways->CreateField(&layer_ways_field_relation) != OGRERR_NONE) {
            cerr << "Creating relation field in table ways failed.\n";
            exit(1);
        }

        OGRFieldDefn layer_ways_field_width("width", OFTReal);
        layer_ways_field_width.SetWidth(10);
        if (m_layer_ways->CreateField(&layer_ways_field_width) != OGRERR_NONE) {
            cerr << "Creating width field in table ways failed.\n";
            exit(1);
        }

        OGRFieldDefn layer_ways_field_lastchange("lastchange", OFTString);
        layer_ways_field_lastchange.SetWidth(20);
        if (m_layer_ways->CreateField(&layer_ways_field_lastchange) != OGRERR_NONE) {
            cerr << "Creating lastchange field in table ways failed.\n";
            exit(1);
        }

        OGRFieldDefn layer_ways_field_construction("construction", OFTString);
        layer_ways_field_construction.SetWidth(7);
        if (m_layer_ways->CreateField(&layer_ways_field_construction) != OGRERR_NONE) {
            cerr << "Creating construction field in table ways failed.\n";
            exit(1);
        }

        OGRFieldDefn layer_ways_field_width_error("width_error", OFTString);
        layer_ways_field_width_error.SetWidth(6);
        if (m_layer_ways->CreateField(&layer_ways_field_width_error) != OGRERR_NONE) {
            cerr << "Creating width_error field in table ways failed.\n";
            exit(1);
        }

        OGRFieldDefn layer_ways_field_tagging_error("tagging_error", OFTString);
        layer_ways_field_type.SetWidth(6);
        if (m_layer_ways->CreateField(&layer_ways_field_tagging_error) != OGRERR_NONE) {
            cerr << "Creating tagging_error field in table ways failed.\n";
            exit(1);
        }

        ogrerr = m_layer_ways->StartTransaction();
        if (ogrerr != OGRERR_NONE) {
            cerr << "Creating ways table failed.\n";
            cerr << "OGRERR: " << ogrerr << endl;
            exit(1);
        }

        /*---- TABLE NODES ----*/
        m_layer_nodes = m_data_source->CreateLayer("nodes", &sparef, wkbPoint, nullptr);
        if (!m_layer_nodes) {
            cerr << "Layer nodes creation failed.\n";
            exit(1);
        }

        OGRFieldDefn layer_nodes_field_id("id", OFTString);
        layer_nodes_field_id.SetWidth(12);
        if (m_layer_nodes->CreateField(&layer_nodes_field_id) != OGRERR_NONE) {
            cerr << "Creating id field in table nodes failed.\n";
            exit(1);
        }

        OGRFieldDefn layer_nodes_field_specific("specific", OFTString);
        layer_nodes_field_specific.SetWidth(11);
        if (m_layer_nodes->CreateField(&layer_nodes_field_specific) != OGRERR_NONE) {
            cerr << "Creating id field in table nodes failed.\n";
            exit(1);
        }

        OGRFieldDefn layer_nodes_field_direction_error("direction_error", OFTString);
        layer_nodes_field_direction_error.SetWidth(6);
        if (m_layer_nodes->CreateField(&layer_nodes_field_direction_error) != OGRERR_NONE) {
            cerr << "Creating direction_error field in table nodes failed.\n";
            exit(1);
        }

        OGRFieldDefn layer_nodes_field_name_error("name_error", OFTString);
        layer_nodes_field_name_error.SetWidth(6);
        if (m_layer_nodes->CreateField(&layer_nodes_field_name_error) != OGRERR_NONE) {
            cerr << "Creating name_error field in table nodes failed.\n";
            exit(1);
        }

        OGRFieldDefn layer_nodes_field_type_error("type_error", OFTString);
        layer_nodes_field_type_error.SetWidth(6);
        if (m_layer_nodes->CreateField(&layer_nodes_field_type_error) != OGRERR_NONE) {
            cerr << "Creating type_error field in table nodes failed.\n";
            exit(1);
        }

        OGRFieldDefn layer_nodes_field_spring_error("spring_error", OFTString);
        layer_nodes_field_spring_error.SetWidth(6);
        if (m_layer_nodes->CreateField(&layer_nodes_field_spring_error) != OGRERR_NONE) {
            cerr << "Creating spring_error field in table nodes failed.\n";
            exit(1);
        }

        OGRFieldDefn layer_nodes_field_end_error("end_error", OFTString);
        layer_nodes_field_end_error.SetWidth(6);
        if (m_layer_nodes->CreateField(&layer_nodes_field_end_error) != OGRERR_NONE) {
            cerr << "Creating end_error field in table nodes failed.\n";
            exit(1);
        }

        OGRFieldDefn layer_nodes_field_way_error("way_error", OFTString);
        layer_nodes_field_way_error.SetWidth(6);
        if (m_layer_nodes->CreateField(&layer_nodes_field_way_error) != OGRERR_NONE) {
            cerr << "Creating way_error field in table nodes failed.\n";
            exit(1);
        }

        ogrerr = m_layer_nodes->StartTransaction();
        if (ogrerr != OGRERR_NONE) {
            cerr << "Creating nodes table failed.\n";
            cerr << "OGRERR: " << ogrerr << endl;
            exit(1);
        }
    }

    const char *get_waterway_type(const char *input) {
        if (!input) {
            return nullptr;
        }
        if ((strcmp(input,"river"))&&(strcmp(input,"stream"))&&(strcmp(input,"drain"))&&(strcmp(input,"brook"))
                &&(strcmp(input,"canal"))&&(strcmp(input,"ditch"))&&(strcmp(input,"riverbank"))) {
            return strdup("other\0");
        } else {
            return strdup(input);
        }
    }

    const string get_timestamp(osmium::Timestamp timestamp) {
        string output = timestamp.to_iso();
        output.replace(10, 1, " ");
        output.replace(19, 1, "");
        return output;
    }

    /***
     * Get width as float in meter from the common formats. Detect errors within the width sting.
     * A ',' as separator dedicates an error, but is handled.
     */
    bool get_width(const char *width_chr, float &width) {
        string width_str = width_chr;
        bool err = false;

        if (width_str.find(",") != string::npos) {
            width_str.replace(width_str.find(","),1,".");
            err = true;
            width_chr = width_str.c_str();
        }

        char *endptr;
        width = strtof(width_chr, &endptr);

        if (endptr == width_chr) {
            width = -1;
        } else if (*endptr) {
           while(isspace(*endptr)) endptr++;
           if (!strcasecmp(endptr, "m")) {
           } else if (!strcasecmp(endptr, "km")) {
               width *=1000;
           } else if (!strcasecmp(endptr, "mi")) {
               width *=1609.344;
           } else if (!strcasecmp(endptr, "nmi")) {
               width *=1852;
           } else if (!strcmp(endptr, "'")) {
               width *= 12.0 * 0.0254;
           } else if (!strcmp(endptr, "\"")) {
               width *= 0.0254;
           } else if (*endptr == '\'') {
               endptr++;
               char *inchptr;
               float inch = strtof(endptr,&inchptr);
               if ((!strcmp(inchptr, "\"")) && (endptr != inchptr)) {
                   width = (width * 12 + inch) * 0.0254;
               } else {
                   width = -1;
                   err = true;
               }
           } else {
               width = -1;
               err = true;
           }
        }
        return err;
    }

public:
    /***
     * node_map: Contains all firstnodes and lastnodes of found waterways with the
     * names and categories of the connected ways.
     * error_map: Contains ids of the potential error nodes (or mouths) to be checked
     * in pass 3.
     * error_tree: The potential error nodes remaining after pass 3 are stored in here
     * for a geometrical analysis in pass 5.
     */
    google::sparse_hash_map<osmium::object_id_type, vector<WaterWay*>> node_map;
    google::sparse_hash_map<osmium::object_id_type, ErrorSum*> error_map;
    geos::index::strtree::STRtree error_tree;

    explicit DataStorage() {
        init_db();
        node_map.set_deleted_key(-1);
        error_map.set_deleted_key(-1);
    }

    ~DataStorage() {
        m_layer_polygons->CommitTransaction();
        m_layer_relations->CommitTransaction();
        m_layer_ways->CommitTransaction();
        m_layer_nodes->CommitTransaction();

        OGRDataSource::DestroyDataSource(m_data_source);
        OGRCleanupAll();
        for (auto wway : WaterWays) {
            delete wway;
        }
    }

    void insert_polygon_feature(OGRGeometry *geom, const osmium::Area &area) {
        osmium::object_id_type way_id;
        osmium::object_id_type relation_id;
        if (area.from_way()) {
            way_id = area.orig_id();
            relation_id = 0;
        } else {
            way_id = 0;
            relation_id = area.orig_id();
        }

        const char *natural = area.get_value_by_key("natural");
        const char *type;
        if ((natural) && (!strcmp(natural, "coastline"))) {
            type = natural;
        } else {
            type = get_waterway_type(area.get_value_by_key("waterway"));
            if (!type) {
                type = area.get_value_by_key("water");
            }
            if (!type) {
                type = area.get_value_by_key("landuse");
            }
            if (!type) type = "";
        }

        const char *name = area.get_value_by_key("name");
        if (!name) name = "";

        OGRFeature *feature = OGRFeature::CreateFeature(m_layer_polygons->GetLayerDefn());
        if (feature->SetGeometry(geom) != OGRERR_NONE) {
            cerr << "Failed to create geometry feature for polygon of ";
            if (area.from_way()) cerr << "way: ";
            else cerr << "relation: ";
            cerr << area.orig_id() << endl;
        }

        feature->SetField("way_id", static_cast<int>(way_id));
        feature->SetField("relation_id", static_cast<int>(relation_id));
        feature->SetField("type", type);
        feature->SetField("name", name);
        feature->SetField("lastchange", get_timestamp(area.timestamp()).c_str());
        if (m_layer_polygons->CreateFeature(feature) != OGRERR_NONE) {
            cerr << "Failed to create polygon feature.\n";
        }
        OGRFeature::DestroyFeature(feature);
    }

    void insert_relation_feature(OGRGeometry *geom, const osmium::Relation &relation, bool contains_nowaterway) {
        const char *type = relation.get_value_by_key("waterway");
        if (!type) type = relation.get_value_by_key("landuse");
        if (!type) type = "";
        const char *name = relation.get_value_by_key("name");
        if (!name) name = "";
        OGRFeature *feature = OGRFeature::CreateFeature(m_layer_relations->GetLayerDefn());
        if (feature->SetGeometry(geom) != OGRERR_NONE) {
            cerr << "Failed to create geometry feature for relation: " << relation.id() << endl;
        }
        feature->SetField("id", static_cast<int>(relation.id()));
        feature->SetField("type", get_waterway_type(type));
        feature->SetField("name", name);
        feature->SetField("lastchange", get_timestamp(relation.timestamp()).c_str());
        if (contains_nowaterway)
            feature->SetField("nowaterway_error", "true");
        else
            feature->SetField("nowaterway_error", "false");
        if (m_layer_relations->CreateFeature(feature) != OGRERR_NONE) {
            cerr << "Failed to create relation feature.\n";
        }
        OGRFeature::DestroyFeature(feature);
    }

    void insert_way_feature(OGRGeometry *geom, const osmium::Way &way, osmium::object_id_type rel_id) {
        const char *natural = way.get_value_by_key("natural");
        const char *type;
        if ((natural) && (!strcmp(natural, "coastline"))) {
            type = natural;
        } else {
            type = get_waterway_type(way.get_value_by_key("waterway"));
            if (!type) type = "";
        }

        const char *name = way.get_value_by_key("name");
        if (!name) name = "";

        const char *width;
        if (way.get_value_by_key("width")) {
            width = way.get_value_by_key("width");
        } else if (way.get_value_by_key("est_width")) {
            width = way.get_value_by_key("est_width");
        } else {
            width = "";
        }
        bool width_err;
        float w = 0;
        width_err = get_width(width,w);

        char firstnode_chr[13], lastnode_chr[13];
        osmium::object_id_type firstnode = way.nodes().cbegin()->ref();
        osmium::object_id_type lastnode = way.nodes().crbegin()->ref();
        sprintf(firstnode_chr, "%ld", firstnode);
        sprintf(lastnode_chr, "%ld", lastnode);

        const char *construction;
        if (way.get_value_by_key("bridge")) {
            construction = "bridge";
        } else if (way.get_value_by_key("tunnel")) {
            construction = "tunnel";
        } else {
            construction = "";
        }

        WaterWay *wway = new WaterWay(firstnode, lastnode, name, type);
        WaterWays.push_back(wway);
        node_map[firstnode].push_back(wway);
        node_map[lastnode].push_back(wway);

        OGRFeature *feature = OGRFeature::CreateFeature(m_layer_ways->GetLayerDefn());
        if (feature->SetGeometry(geom) != OGRERR_NONE) {
            cerr << "Failed to create geometry feature for way: " << way.id() << endl;
        }
        feature->SetField("id", static_cast<int>(way.id()));
        feature->SetField("type", type);
        feature->SetField("name", name);
        feature->SetField("firstnode", firstnode_chr);
        feature->SetField("lastnode", lastnode_chr);
        feature->SetField("relation", static_cast<int>(rel_id));
        feature->SetField("lastchange", get_timestamp(way.timestamp()).c_str());
        feature->SetField("construction", construction);
        feature->SetField("width_error", (width_err) ? "true" : "false");
        if (w>-1) feature->SetField("width", w);

        if (m_layer_ways->CreateFeature(feature) != OGRERR_NONE) {
            cerr << "Failed to create way feature.\n";
        }

        OGRFeature::DestroyFeature(feature);
    }

    void insert_node_feature(osmium::Location location, osmium::object_id_type node_id, ErrorSum *sum) {
        osmium::geom::OGRFactory<> ogr_factory;
        OGRFeature *feature = OGRFeature::CreateFeature(m_layer_nodes->GetLayerDefn());
        OGRPoint *point;
        try {
             point = ogr_factory.create_point(location).release();
        } catch (osmium::geometry_error) {
            cerr << "Error at node: " << node_id << endl;
        } catch (...) {
            cerr << "Error at node: " << node_id << endl << "Unexpected error" << endl;
        }
        char id_chr[12];
        sprintf(id_chr, "%ld", node_id);

        if ((point) && (feature->SetGeometry(point) != OGRERR_NONE)) {
            cerr << "Failed to create geometry feature for node: " << node_id << endl;
        }
        feature->SetField("id", id_chr);
        if (sum->is_rivermouth()) feature->SetField("specific", "rivermouth");
        else feature->SetField("specific", (sum->is_outflow()) ? "outflow": "");
        feature->SetField("direction_error", (sum->is_direction_error()) ? "true" : "false");
        feature->SetField("name_error", (sum->is_name_error()) ? "true" : "false");
        feature->SetField("type_error", (sum->is_type_error()) ? "true" : "false");
        feature->SetField("spring_error", (sum->is_spring_error()) ? "true" : "false");
        feature->SetField("end_error", (sum->is_end_error()) ? "true" : "false");
        feature->SetField("way_error", (sum->is_way_error()) ? "true" : "false");

        if (m_layer_nodes->CreateFeature(feature) != OGRERR_NONE) {
            cerr << "Failed to create node feature.\n";
        }
        OGRFeature::DestroyFeature(feature);
        if (point) OGRGeometryFactory::destroyGeometry(point);
    }

    /***
     * unused: Change boolean value in already inserted rows.
     */
    void change_bool_feature(char table, const long fid, const char *field, const char *value, char *error_advice) {
        OGRFeature *feature;
        OGRLayer *layer;
        switch (table) {
            case 'r':
                layer = m_layer_relations;
                break;
            case 'w':
                layer = m_layer_ways;
                break;
            case 'n':
                layer = m_layer_nodes;
                break;
            default:
                cerr << "change_bool_feature expects {'r','w','n'} = {relations, ways, nodes}" << endl;
                exit(1);
        }
        feature = layer->GetFeature(fid);
        feature->SetField(field, value);
        if (layer->SetFeature(feature) != OGRERR_NONE) {
            cerr << "Failed to change boolean field " << error_advice << endl;
        }
        OGRFeature::DestroyFeature(feature);
    }

    /***
     * Insert the error nodes remaining after first indicate false positives in pass 3 into the error_tree.
     * FIXME: memory for point isn't free'd
     */
    void init_tree(location_handler_type &locationhandler) {
        osmium::geom::GEOSFactory<> geos_factory;
        geos::geom::Point *point;
        for (auto& node : error_map) {
            if (!(node.second->is_rivermouth()) && (!(node.second->is_outflow()))) {
                point = geos_factory.create_point(locationhandler.get_node_location(node.first)).release();
                error_tree.insert(point->getEnvelopeInternal(), (osmium::object_id_type *) &(node.first));
            }
        }
    }

    /***
     * Insert the error nodes into the nodes table.
     */
    void insert_error_nodes(location_handler_type &locationhandler) {
        osmium::Location location;
        for (auto node : error_map) {
            node.second->switch_poss();
            osmium::object_id_type node_id = node.first;
            location = locationhandler.get_node_location(node_id);
            insert_node_feature(location,node_id,node.second);
            delete node.second;
        }
    }
};
