/* h16-plt2ps.cpp: Convert plot file from H316 PLT device to postscript

   Copyright (c) 2008, 2012, Adrian P. Wise

   Permission is hereby granted, free of charge, to any person obtaining a
   copy of this software and associated documentation files (the "Software"),
   to deal in the Software without restriction, including without limitation
   the rights to use, copy, modify, merge, publish, distribute, sublicense,
   and/or sell copies of the Software, and to permit persons to whom the
   Software is furnished to do so, subject to the following conditions:

   The above copyright notice and this permission notice shall be included in
   all copies or substantial portions of the Software.

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
   IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
   ADRIAN P WISE BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

   Except as contained in this notice, the name of Adrian P Wise shall not be
   used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from Adrian P Wise.
*/

#include <cstdlib>
#include <cstring>
#include <cmath>

#include <iostream>
#include <fstream>
#include <sstream>

#include <string>
#include <list>

// This is used because Postscript files are supposed
// to have CR-LF as a line-end, not a UNIX-style LF only
#define ENDL "\r\n"

#define CREATOR "h16-plt2ps"

#define ENV_MODEL     "H16PLT2PS_MODEL"
#define ENV_MEDIA     "H16PLT2PS_MEDIA"
#define ENV_PEN_WIDTH "H16PLT2PS_PEN_WIDTH"

#define DEFAULT_MODEL     "2113"
#define DEFAULT_MEDIA     "A4"
#define DEFAULT_PEN_WIDTH 0.5

struct PlotterModel;
struct Media;

class PlotFile {

public:
  PlotFile();
  ~PlotFile();

  void readfile(std::istream &ins, bool ascii_file);
  void preprocess(bool scale_flag,
                  bool keep_flag,
                  bool force_portrait,
                  bool force_landscape,
                  const PlotterModel *plotter_model,
                  const Media *media,
                  double pen_width);
  void headers(bool epsf_flag, bool keep_flag, std::ostream &outs, std::string title);
  void data(std::ostream &outs);
  void footers(std::ostream &outs);

private:
  enum PLT_DIRN {
    PD_NULL = 000,
    PD_N  = 004,
    PD_NE = 005,
    PD_E  = 001,
    PD_SE = 011,
    PD_S  = 010,
    PD_SW = 012,
    PD_W  = 002,
    PD_NW = 006,
    PD_UP = 016,
    PD_DN = 014,
  
    PD_NUM
  };

  static const char *pd_names[16];
  
  int x_pos, y_pos;
  bool pen;

  int x_offset, y_offset;
  double scale;

  double pen_steps;

  bool landscape;
  int x_page_pt, y_page_pt;
  int bound_ll_x, bound_ll_y;
  int bound_ur_x, bound_ur_y;

  std::string media_name;

  class Segment {
  public:
    Segment(PLT_DIRN d, int c);

    void direction(PLT_DIRN d) {Direction = d;};
    PLT_DIRN direction() const {return Direction;};

    void count(int c) {Count = c;};
    int count() const {return Count;};

  private:
    PLT_DIRN Direction;
    int Count;
  };

  std::list<Segment> segments;
  void apply_segment(const Segment &seg);
  std::string translate(int x, int y);
};

const char *PlotFile::pd_names[16] = {
  "(null)", "E",       "W",  "(error)",
  "N",      "NE",      "NW", "(error)",
  "S",      "SE",      "SW", "(error)",
  "DN",     "(error)", "UP", "(error)" 
};

struct PlotterModel {
  const char **names;
  bool metric;
  int step; // In 0.1mm or mil units
  int paper_width; // In 0.1mm or mil units
  int limit_width; // In 0.1mm or mil units
};

struct Media {
  const char *name;
  int x;
  int y;
};

PlotFile::Segment::Segment(PLT_DIRN d, int c)
  : Direction(d),
    Count(c)
{
}

PlotFile::PlotFile()  
  : x_pos(0),
    y_pos(0),
    pen(false)
{
}

PlotFile::~PlotFile()
{
}

void PlotFile::readfile(std::istream &ins, bool ascii_file)
{
  PLT_DIRN current_direction;
  long current_count;

  while (ins) {
    current_direction = PD_NULL;
    current_count = 0;

    if (ascii_file) {
      char buf[100];
      int d;
      char *cp, *cp2;

      ins.getline(buf, 100);
      //std::cout << "Current line <" << buf << ">" << std::endl;

      if ((ins) && (strlen(buf) > 0)) {
        for (d = 15; d >= 0; d--) { // Note two-character cases first

          if ((pd_names[d][0] != '(') &&
              (strncmp(pd_names[d], buf, strlen(pd_names[d])) == 0)) {
            current_direction = (PLT_DIRN)(d);

            cp = &buf[strlen(pd_names[d])];
            while ((*cp) && isspace(*cp) && (*cp != '\n'))
              cp++;

            if ((*cp) && (*cp != '\n')) {
              // Is there a number
              current_count = strtol(cp, &cp2, 0);
              if (cp2 <= cp)
                current_direction = PD_NULL;
            }

            break;
          }
        }

        if (current_direction == PD_NULL) {
          std::cerr << "Could not parse line <" << buf << ">" << std::endl;
          exit(1);
        }
      }

    } else {
      // Binary file reader...
      
      int c = ins.get();
        
      if (ins) {
        while ((ins) && ((c & 0200) != 0)) {
          // This is a prefix
          //printf("Prefix = %02x\n", c);
          current_count = (current_count << 7) | (c & 0177);
          c = ins.get();
        }

        if (ins) {
          // This is the actual command byte
          current_count     = (current_count << 3) | (c & 0007);
          current_direction = (PLT_DIRN) ((c >> 3) & 0017);

          if ((current_direction == PD_NULL) ||
              (current_direction == 003) ||
              (current_direction == 007) ||
              (current_direction == 013) ||
              (current_direction == 017)) {
            std::cerr << "Bad pen direction command" << std::endl;
            exit(2);
          }
        } else {
          std::cerr << "Unexpected end of file between prefix and command" << std::endl;
          exit(2);
        }
      }
    }

    if (current_direction != PD_NULL) {
      Segment segment(current_direction, current_count);
      segments.push_back(segment);
    }

  }

}

void PlotFile::apply_segment(const Segment &seg)
{
  PLT_DIRN d = seg.direction();
  int c = seg.count() + 1;
  
  switch(d) {
  case PD_N:  y_pos+=c;           break;
  case PD_NE: y_pos+=c; x_pos+=c; break;
  case PD_E:            x_pos+=c; break;
  case PD_SE: y_pos-=c; x_pos+=c; break;
  case PD_S:  y_pos-=c;           break;
  case PD_SW: y_pos-=c; x_pos-=c; break;
  case PD_W:            x_pos-=c; break;
  case PD_NW: y_pos+=c; x_pos-=c; break;
  case PD_UP: pen = false; break;
  case PD_DN: pen = true;  break;
  default:
    abort();
  }
}

std::string PlotFile::translate(int x, int y)
{
  std::ostringstream ost;
  int tx, ty;
  tx = x + x_offset;
  ty = y + y_offset;
  ost << tx << " " << ty;
  return ost.str();
}

void PlotFile::preprocess(bool scale_flag,
                          bool keep_flag,
                          bool force_portrait,
                          bool force_landscape,
                          const PlotterModel *plotter_model,
                          const Media *media,
                          double pen_width)
{
  std::list<Segment>::const_iterator i;

  x_pos = 0; y_pos = 0; pen = false;

  bool pen_has_been_down = false;

  int x_max = 0;
  int x_min = 0;
  int y_max = 0;
  int y_min = 0;

  int ix_min = ((~((unsigned int)0)) >> 1); // Most +ve
  int ix_max = ~ix_min; // Most -ve
  int iy_max = ix_max;
  int iy_min = ix_min;

  int /*x_range,*/ y_range;
  int ix_range, iy_range;

  // Find the extent of the image, both all movements and
  // that with the pen down

  for (i = segments.begin(); i != segments.end(); i++) {
    apply_segment(*i);

    if (x_pos > x_max) x_max = x_pos;
    if (x_pos < x_min) x_min = x_pos;
    if (y_pos > y_max) y_max = y_pos;
    if (y_pos < y_min) y_min = y_pos;
    
    if (pen) {
      pen_has_been_down = true;
      if (x_pos > ix_max) ix_max = x_pos;
      if (x_pos < ix_min) ix_min = x_pos;
      if (y_pos > iy_max) iy_max = y_pos;
      if (y_pos < iy_min) iy_min = y_pos;
    }

    //std::cout << "pos = (" << x_pos << ", " << y_pos << ") pen = " << pen
    //         << " imin = (" << ix_min << ", " << iy_min << ")" 
    //         << " imax = (" << ix_max << ", " << iy_max << ")" << std::endl;
  }

  if (!pen_has_been_down) {
    std::cerr << "Pen never went down" << std::endl;
    exit(2);
  }

  //std::cout << "min = (" << x_min << ", " << y_min << ")" << std::endl;
  //std::cout << "max = (" << x_max << ", " << y_max << ")" << std::endl;
  //std::cout << "imin = (" << ix_min << ", " << iy_min << ")" << std::endl;
  //std::cout << "imax = (" << ix_max << ", " << iy_max << ")" << std::endl;
  
  //x_range = 1 + x_max - x_min;
  y_range = 1 + y_max - y_min;

  ix_range = 1 + ix_max - ix_min;
  iy_range = 1 + iy_max - iy_min;

  // Figure out whether to use landscape
  landscape = false;
  if ((force_portrait) || (keep_flag))
    landscape = false;
  else if (force_landscape)
    landscape = true;
  else {
    //
    // Look at the width and height of the image
    // to see whether we'd choose portrait or
    // landscape.
    //
    landscape = (ix_range > iy_range);
  }

  // Look at the characteristics of the plotter
  int paper_steps = plotter_model->paper_width/plotter_model->step;
  int limit_steps = plotter_model->limit_width/plotter_model->step;
  int margin = ((paper_steps - limit_steps) / 2);

  double step_size_pt = 72.0 * ((plotter_model->metric) ?
                                (plotter_model->step * 0.1 / 25.4) :
                                (plotter_model->step / 1000.0));

  double x_scale, y_scale;

  if (keep_flag) {
    media_name = "Custom";
    x_page_pt = paper_steps * step_size_pt;
    y_page_pt = (y_range + (2 * margin) ) * step_size_pt;

    scale = step_size_pt;

    x_offset = margin - x_min;
    y_offset = margin - y_min;

  } else {
    media_name = media->name;
    x_page_pt = (landscape) ? media->y : media->x;
    y_page_pt = (landscape) ? media->x : media->y;
    
    if (scale_flag) {
      // Take the width of the image, adding 5% first...
      x_scale = x_page_pt / (ix_range * 1.05);

      // The the height
      y_scale = y_page_pt / (iy_range * 1.05);

      //std::cout << "x_scale = " << x_scale << " y_scale = " << y_scale << std::endl;

      scale = (x_scale < y_scale) ? x_scale : y_scale;

      // Centre image on page
      
      x_offset = floor((x_page_pt/scale - ix_range)/2) - ix_min;
      y_offset = floor((y_page_pt/scale - iy_range)/2) - iy_min;

    } else {  
      // Calculate a suitable scale factor

      // Take the width of the paper first...
      x_scale = ((double) x_page_pt) / paper_steps;

      // For the height, take the actual number of steps
      // traversed and add on 5%
      y_scale = ((double) y_page_pt) / (1.05 * y_range);

      //std::cout << "x_scale = " << x_scale << " y_scale = " << y_scale << std::endl;

      bool limit_is_x = (x_scale < y_scale);

      scale = (limit_is_x) ? x_scale : y_scale;

      // Figure suitable offsets
      if (limit_is_x) {
        x_offset = margin - x_min;
        // y-dimension has spare so half to top, half to bottom
        int half_spare = (int) (((((double) y_page_pt) / scale) - y_range) / 2.0);
        y_offset = half_spare - y_min;
      } else {
        y_offset = margin - y_min;
        // x-dimension has spare so half to left, half to right
        int half_spare = (int) (((((double) x_page_pt) / scale) - limit_steps) / 2.0);
        //std::cout << "half_spare = " << half_spare << std::endl;
        x_offset = half_spare - x_min;
      }
    }
  }

  double pen_pt = 72.0 * pen_width / 25.4;
  pen_steps = pen_pt / step_size_pt;

  //std::cout << "offset = (" << x_offset << ", " << y_offset << ")" << std::endl;
  //std::cout << "range = (" << x_range << ", " << y_range << ")" << std::endl;
  //std::cout << "irange = (" << ix_range << ", " << iy_range << ")" << std::endl;

  // Calculate bounding box

  bound_ll_x = floor( (scale * (ix_min+x_offset)) - (pen_pt/2.0) );
  bound_ll_y = floor( (scale * (iy_min+y_offset)) - (pen_pt/2.0) );
  bound_ur_x = ceil( (scale * (ix_max+x_offset)) + (pen_pt/2.0) );
  bound_ur_y = ceil( (scale * (iy_max+y_offset)) + (pen_pt/2.0) );

}

void PlotFile::headers(bool epsf_flag, bool keep_flag, std::ostream &outs, std::string title)
{
  outs << "%!PS-Adobe-3.0";
  if (epsf_flag)
    outs << " EPSF-3.0";
  outs << ENDL;
  outs << "%%BoundingBox: "
       << bound_ll_x << " " << bound_ll_y << " "
       << bound_ur_x << " " << bound_ur_y << ENDL;
  outs << "%%Creator: " << CREATOR << ENDL;
  outs << "%%DocumentData: Clean7Bit" << ENDL;

  time_t t;
  char buf[100];
  (void) time(&t);
  ctime_r(&t, buf);
  if ((*buf) && (buf[strlen(buf)-1]=='\n'))
    buf[strlen(buf)-1]='\0';

  outs << "%%CreationDate: (" << buf << ")" << ENDL;
  outs << "%%DocumentMedia: " << media_name << " "
       << x_page_pt << " " << y_page_pt
       << " ( ) ( )" << ENDL;
  outs << "%%LanguageLevel: 1" << ENDL;
  if (!keep_flag)
    outs << "%%Orientation: "
         << ((landscape) ? "Landscape" : "Portrait") << ENDL;
  outs << "%%Pages: 1" << ENDL;
  outs << "%%Title: (" << title << ")" << ENDL;
  outs << "%%EndComments" << ENDL;
  outs << "%%Page: 1 1" << ENDL;
  outs << "%%PageMedia: " << media_name << ENDL;
  outs << "%%BeginPageSetup" << ENDL;
  outs << "/pgsave save def" << ENDL;
  outs << "%%EndPageSetup" << ENDL;

  outs << scale << " " << scale << " scale" << ENDL;
  outs << "1 setlinecap" << ENDL;
  outs << "1 setlinejoin" << ENDL;
  outs << pen_steps << " setlinewidth" << ENDL;

}

void PlotFile::footers(std::ostream &outs)
{
 outs << "pgsave restore" << ENDL;
 outs << "showpage" << ENDL;
 outs << "%%EOF" << ENDL;
}

void PlotFile::data(std::ostream &outs)
{
  std::list<Segment>::const_iterator i;

  x_pos = 0; y_pos = 0; pen = false;

  bool drawing = false;
  bool prev_pen;

  for (i = segments.begin(); i != segments.end(); i++) {
    prev_pen = pen;
    apply_segment(*i);

    if (pen != prev_pen) {
      if (pen) {
        outs << "newpath" << ENDL;
        outs << translate(x_pos, y_pos) << " moveto" << ENDL;
        drawing = true;
      } else {
        outs << "stroke" << ENDL;
        drawing = false;
      }
    } else if (drawing) {
      outs << translate(x_pos, y_pos) << " lineto" << ENDL;
    }
  }
}

/*
 * Model Option   Step   Paper (steps) Limits (steps)
 *  3341   2113  0.1mm   360mm   3600   340mm   3400
 *  3342   2114  0.2mm   360mm   1800   340mm   1700
 *  3141   2111   5mil  14.125   2825  13.375   2675
 *  3142   2112  10mil  14.125   1412  13.375   1337
 *
 */

static const char *m3341_names[] = {
  "3341", "341", "2113", "13", "3", 0 };
static const char *m3342_names[] = {
  "3342", "342", "2114", "14", "4", 0 };
static const char *m3141_names[] = {
  "3141", "141", "2111", "11", "1", 0 };
static const char *m3142_names[] = {
  "3142", "142", "2112", "12", "2", 0 };
static const PlotterModel plotter_models[] = {
  {m3341_names, true,   1,  3600,  3400},
  {m3342_names, true,   2,  3600,  3400},
  {m3141_names, false,  5, 14125, 13375},
  {m3142_names, false, 10, 14125, 13375},
  {0,           false,  0,     0,     0}
};

static const Media medias[] = {
  {"Folio",      595,  935},
  {"Executive",  522,  756},
  {"Letter",     612,  792},
  {"Legal",      612, 1008},
  {"Ledger",    1224,  792},
  {"Tabloid",    792, 1224},
  {"A0",        2384, 3370},
  {"A1",        1684, 2384},
  {"A2",        1191, 1684},
  {"A3",         842, 1191},
  {"A4",         595,  842},
  {"A5",         420,  595},
  {"A6",         297,  420},
  {"A7",         210,  297},
  {"A8",         148,  210},
  {"A9",         105,  148},
  {"B0",        2920, 4127},
  {"B1",        2064, 2920},
  {"B2",        1460, 2064},
  {"B3",        1032, 1460},
  {"B4",         729, 1032},
  {"B5",         516,  729},
  {"B6",         363,  516},
  {"B7",         258,  363},
  {"B8",         181,  258},
  {"B9",         127,  181}, 
  {"B10",         91,  127},
  {0,              0,    0}
};


static const PlotterModel *lookup_plotter_model(const std::string &str)
{
  const PlotterModel *pm = plotter_models;

  while (pm->names) {
    const char **mn = pm->names;
    while (*mn) {
      std::string smn(*mn);
      if (smn == str) {
        return pm;
      }
      mn++;
    }
    pm++;
  }
  return 0;
}

static const Media *lookup_media(const std::string &str)
{
  const Media *m = medias;

  // Should ignore case?

  while (m->name) {
    std::string sm(m->name);
    if (sm == str) {
      return m;
    }
    m++;
  }
  return 0;
}

static bool parse_double(const std::string &str, double &v)
{
  const char *nptr = str.c_str();
  char *endptr;
  v = strtod(nptr, &endptr);
  return ((endptr-nptr) == ((signed long) str.size()));
}

static bool ascii_file;
static char *input_filename;
static char *output_filename;
static const PlotterModel *plotter_model;
static const Media *media;
static double pen_width;
static bool epsf_flag;
static bool scale_flag;
static bool keep_flag;
static bool force_portrait;
static bool force_landscape;
static std::string title;


static void defs()
{
  ascii_file = false;
  epsf_flag = false;
  input_filename = 0;
  output_filename = 0;
  plotter_model = lookup_plotter_model(DEFAULT_MODEL);
  media = lookup_media(DEFAULT_MEDIA);
  pen_width = DEFAULT_PEN_WIDTH;
  scale_flag = false;
  keep_flag = false;
  force_portrait = false;
  force_landscape = false;
  title = "";
}

static void envs()
{
  const char *value;

  if ( (value = getenv(ENV_MODEL)) ) {
    const PlotterModel *pm = lookup_plotter_model(value);
    if (pm)
      plotter_model = pm;
    else {
      std::cerr << "Warning: Couldn't identify model from "
                << ENV_MODEL << " set to <"
                << value << ">" << std::endl;
    }
  }

  if ( (value = getenv(ENV_MEDIA)) ) {
    const Media *m = lookup_media(value);
    if (m)
      media = m;
    else {
      std::cerr << "Warning: Couldn't identify media from "
                << ENV_MEDIA << " set to <"
                << value << ">" << std::endl;
    }
  }

  if ( (value = getenv(ENV_PEN_WIDTH)) ) {
    double w;
    if (parse_double(value, w))
      pen_width = w;
    else {
      std::cerr << "Warning: Couldn't parse pen width from "
                << ENV_PEN_WIDTH << " set to <"
                << value << ">" << std::endl;
    }
  }

}

static void print_usage(std::ostream &strm, char *arg0)
{
  strm << "Usage : " << arg0
       << " [-h] [-f<l|p>] [-i] [-k] [-m <media>] [-o <filename>] [-p <model>] [-w <width>] [-a] <filename>" << std::endl;
}

static void args(int argc, char **argv)
{
  int a;

  bool usage = false;
  bool help = false;

  bool media_set_by_args = false;
  bool title_set = false;

  bool seen_filename = false;

  for (a=1; a<argc; a++) {
    if ((argv[a][0] == '-') && (argv[a][1])) { // '-' on its own is a filename
      if (strcmp(argv[a], "-a") == 0) {
        ascii_file = true;
      } else if (strcmp(argv[a], "-e") == 0) {
        epsf_flag = true;
      } else if (strncmp(argv[a], "-f", 2) == 0) {
        char c = '\0';
        if (strlen(argv[a]) == 2) {
          a++;
          if ((a<argc) && 
              (strlen(argv[a]) == 1))
            c = argv[a][0];
          else {usage = true; break; }
        } else if (strlen(argv[a]) == 3) {
          c = argv[a][2];
        } else { usage = true; break; }
        c = tolower(c);
        //std::cout << "c = <" << c << ">" << std::endl;
        if (c == 'p')
          force_portrait = true;
        else if (c == 'l')
          force_landscape = true;
        else { usage = true; break; }
      } else if ((strncmp(argv[a], "-h", 2) == 0) || (strncmp(argv[a], "--h", 3) == 0)) {
        help = true;
        break;
      } else if (strcmp(argv[a], "-k") == 0) {
        keep_flag = true;
      } else if (strcmp(argv[a], "-m") == 0) {
        a++;
        if ((a<argc) &&
            (media = lookup_media(argv[a])))
          media_set_by_args = true;
        else {
          usage = true;
          break;
        }
      } else if (strcmp(argv[a], "-o") == 0) {
        a++;
        if (a<argc)
          output_filename = strdup(argv[a]);
        else {
          usage = true;
          break;
        }
      } else if (strcmp(argv[a], "-p") == 0) {
        a++;
        if ((a>=argc) ||
            (!(plotter_model = lookup_plotter_model(argv[a])))) {
          usage = true;
          break;
        }
      } else if (strcmp(argv[a], "-s") == 0) {
        scale_flag = true;
      } else if (strcmp(argv[a], "-t") == 0) {
        a++;
        if (a<argc) {
          title = argv[a];
          title_set = true;
        } else {
          usage = true;
          break;
        }
      } else if (strcmp(argv[a], "-w") == 0) {
        a++;
        if ((a>=argc) ||
            (!parse_double(argv[a], pen_width))) {
          usage = true;
          break;
        }
      } else {
        usage = true;
        break;
      }
    } else {
      if (a == (argc-1)) {
        seen_filename = true;
        if (strcmp(argv[a], "-") == 0)
          input_filename = 0;
        else
          input_filename = strdup(argv[a]);
      }
      else {
        usage = true;
        break;
      }
    }
  }

  if (help) {
    print_usage(std::cout, argv[0]);

    std::cout << " -a            : ASCII input file format" << std::endl;
    std::cout << " -e            : Produce EPSF" << std::endl;
    std::cout << " -fl           : Force landscape" << std::endl;
    std::cout << " -fp           : Force portrait" << std::endl;
    std::cout << " -h            : This help" << std::endl;
    std::cout << " -s            : Scale image to fit paper" << std::endl;
    std::cout << " -k            : Keep actual plotter paper size" << std::endl;
    std::cout << " -m <media>    : Select media size" << std::endl;
    std::cout << "     media     = \"A4\", \"Letter\", etc." << std::endl;
    std::cout << " -o <filename> : Set output file (else stdout)" << std::endl;
    std::cout << " -p <plotter>  : Select plotter" << std::endl;
    std::cout << "     plotter   - either plotter model," << std::endl;
    std::cout << "                 i.e.:3341, 3342, 3141, 3142" << std::endl;
    std::cout << "               - or Honeywell option number," << std::endl;
    std::cout << "                 i.e.:2111, 2112, 2113, 2114" << std::endl;
    std::cout << " -t <text>     : Supply a title" << std::endl;
    std::cout << " -w <float>    : Width of pen in mm" << std::endl;
    std::cout << " <filename>    : Input plot file" << std::endl;
    std::cout << std::endl;
    std::cout << "Environment variables may be used to specify" << std::endl;
    std::cout << "defaults for some of these arguments:" << std::endl;
    std::cout << ENV_MEDIA << " sets the default media, else \"A4\"" << std::endl;
    std::cout << ENV_MODEL << " sets the default plotter, else \"3341\"/\"2113\"" << std::endl;
    std::cout << ENV_PEN_WIDTH << " sets the default pen width, else 0.5mm" << std::endl;

    exit(0);
  }

  if (!seen_filename)
    usage = true;

  if (force_portrait && force_landscape) {
    std::cerr << "Can't force both portrait and landscape" << std::endl;
    exit(1);
  }

  if (keep_flag && scale_flag) {
    std::cerr << "Doesn't make sense to ask for reproduction at full size" << std::endl
              << "and ask for the image to be scaled to fit the paper" << std::endl;
    exit(1);
  }
 
  // If media set by environment variable then silently
  // ignore, but if set by args, can't have a -k
  if (keep_flag && media_set_by_args) {
    std::cerr << "Doesn't make sense to ask for reproduction at full size" << std::endl
              << "and specify a particular media size" << std::endl;
    exit(1);
  }

  if (usage) {
    print_usage(std::cerr, argv[0]);
    exit(1);
  }

  if (!title_set) {
    const char *s = (input_filename) ? basename(input_filename) : "(stdin)" ;
    title = s;
  }
}

int main (int argc, char **argv)
{
  defs(); // Set default values
  envs(); // Values from environment variables
  args(argc, argv); // Command line arguments
  
  std::ifstream infs;

  if (input_filename) {
    infs.open(input_filename);
    
    if (!infs) {
      std::cerr << "Cannot open <" << input_filename << "> for input" << std::endl;
      exit(1);
    }
  }

  std::istream &ins = (input_filename) ? infs : std::cin;

  std::ofstream outfs;

  if (output_filename) {
    outfs.open(output_filename, std::ios_base::binary);

    if (!outfs) {
      std::cerr << "Cannot open <" << output_filename << "> for output" << std::endl;
      exit(1);
    }
  }

  std::ostream &outs = (output_filename) ? outfs : std::cout;

  PlotFile pf;

  pf.readfile(ins, ascii_file);
  pf.preprocess(scale_flag, keep_flag, force_portrait, force_landscape,
                plotter_model, media, pen_width);
  pf.headers(epsf_flag, keep_flag, outs, title);
  pf.data(outs);
  pf.footers(outs);

  if (output_filename)
    outfs.close();

  if (input_filename)
    infs.close();

  exit(0);
}
