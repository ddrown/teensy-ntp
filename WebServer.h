#pragma once

typedef const char *(*generatePage)(const char *filename);
struct webpage {
  const char *content;
  const char *filename;
  const generatePage pageGen;
};

class WebServer {
  public:
    void begin();
    void setWebpages(const struct webpage *new_webpages);
    const char *getPage(const char *name);

  private:
    const struct webpage *webpages;  
};

extern WebServer webserver;
