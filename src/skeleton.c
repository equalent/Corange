#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "SDL/SDL_rwops.h"
#include "SDL/SDL_local.h"

#include "error.h"

#include "skeleton.h"

skeleton* skeleton_new() {
  skeleton* s =  malloc(sizeof(skeleton));
  s->num_bones = 0;
  s->bones = malloc(sizeof(bone*) * s->num_bones);
  
  s->transforms = malloc(sizeof(matrix_4x4) * s->num_bones);
  s->inv_transforms = malloc(sizeof(matrix_4x4) * s->num_bones);
  
  return s;
}

skeleton* skeleton_copy(skeleton* old) {
  skeleton* new =  malloc(sizeof(skeleton));
  new->num_bones = old->num_bones;
  new->bones = malloc(sizeof(bone*) * new->num_bones);
  
  for(int i = 0; i < new->num_bones; i++) {
    new->bones[i] = bone_new(old->bones[i]->id, old->bones[i]->name);
    new->bones[i]->position = old->bones[i]->position;
    new->bones[i]->rotation = old->bones[i]->rotation;
  }
  
  for(int i = 0; i < new->num_bones; i++) {
    if (old->bones[i]->parent == NULL) {
      new->bones[i]->parent = NULL;
    } else {
      new->bones[i]->parent = skeleton_bone_id(new, old->bones[i]->parent->id);
    }
  }
  
  new->transforms = malloc(sizeof(matrix_4x4) * new->num_bones);
  new->inv_transforms = malloc(sizeof(matrix_4x4) * new->num_bones);
  
  for(int i = 0; i < new->num_bones; i++) {
    new->transforms[i] = old->transforms[i];
    new->inv_transforms[i] = old->inv_transforms[i];
  }
  
  return new;
}


void skeleton_delete(skeleton* s) {

  for(int i = 0; i < s->num_bones; i++) {
    bone_delete(s->bones[i]);
  }
  free(s->bones);
  free(s->transforms);
  free(s->inv_transforms);
  free(s);
}

void skeleton_add_bone(skeleton* s, char* name, int id, int parent_id) {
  
  bone* b = bone_new(id, name);
  b->parent = skeleton_bone_id(s, parent_id);
  
  s->num_bones++;
  s->bones = realloc(s->bones, sizeof(bone*) * s->num_bones);
  s->bones[s->num_bones-1] = b;
  
  s->transforms = realloc(s->transforms, sizeof(matrix_4x4) * s->num_bones);
  s->transforms[s->num_bones-1] = m44_id();
  
  s->inv_transforms = realloc(s->inv_transforms, sizeof(matrix_4x4) * s->num_bones);
  s->inv_transforms[s->num_bones-1] = m44_id();
  
}

void skeleton_print(skeleton* s) {
  for(int i = 0; i < s->num_bones; i++) {
    bone* b = s->bones[i];
    printf("Bone %i: %i %s ", i, b->id, b->name);
    v3_print(b->position);printf(" ");
    //v4_print(b->rotation);
    if (b->parent == NULL) {
      printf(" ROOT\n");
    } else {
      printf(" %i\n", b->parent->id);
    }
  }
}

bone* skeleton_bone_id(skeleton* s, int id) {
  
  if(id == -1) {
    return NULL;
  }
  
  for(int i = 0; i < s->num_bones; i++) {
    if (s->bones[i]->id == id) {
      return s->bones[i];
    }
  }
  
  warning("unknown bone id %i", id);
  return NULL;
}

bone* bone_new(int id, char* name) {
  bone* b = malloc(sizeof(bone));
  b->name = malloc(strlen(name) + 1);
  strcpy(b->name, name);
  b->id = id;
  b->position = v3_zero();
  b->rotation = m44_id();
  b->parent = NULL;
  return b;
}

void bone_delete(bone* b) {
  free(b->name);
  free(b);
}

matrix_4x4 bone_transform(bone* b) {
  
  if (b->parent == NULL) {
    matrix_4x4 ret = m44_id();
    matrix_4x4 trans = m44_translation(b->position);
    matrix_4x4 rot = b->rotation;
    
    ret = m44_mul_m44(ret, trans);
    ret = m44_mul_m44(ret, rot);
    
    return ret;
  } else {
    matrix_4x4 prev = bone_transform(b->parent);
    
    matrix_4x4 ret = m44_id();
    matrix_4x4 trans = m44_translation(b->position);
    matrix_4x4 rot = b->rotation;
    
    ret = m44_mul_m44(ret, prev);
    ret = m44_mul_m44(ret, trans);
    ret = m44_mul_m44(ret, rot);
    
    return ret;
  }
}

/* TODO: These functions could be optimised to use previously calculated transforms */
void skeleton_gen_transforms(skeleton* s) {
  for(int i = 0; i < s->num_bones; i++) {
    s->transforms[i] = bone_transform(s->bones[i]);
  }
}

void skeleton_gen_inv_transforms(skeleton* s) {
  for(int i = 0; i < s->num_bones; i++) {
    s->transforms[i] = bone_transform(s->bones[i]);
    s->inv_transforms[i] = m44_inverse(s->transforms[i]);
  }
}

static int state_load_empty = 0;
static int state_load_nodes = 1;
static int state_load_skeleton = 2;

skeleton* skl_load_file(char* filename) {
  
  int state = state_load_empty;
  
  skeleton* s =  skeleton_new();
  
  SDL_RWops* file = SDL_RWFromFile(filename, "r");
  
  if(file == NULL) {
    error("Could not load file %s", filename);
  }
  
  char line[1024];
  while(SDL_RWreadline(file, line, 1024)) {
    
    if (state == state_load_empty) {
      
      int version;
      if (sscanf(line, "version %i", &version) > 0) {
        if (version != 1) {
          error("Can't load skl file %s. Don't know how to load version %i\n", filename, version);
        }
      }
      
      if (strstr(line, "nodes")) {
        state = state_load_nodes;
      }
      
      if (strstr(line, "skeleton")) {
        state = state_load_skeleton;
      }
    }
    
    else if (state == state_load_nodes) {
      char name[1024];
      int id, parent_id;
      if (sscanf(line, "%i %1024s %i", &id, name, &parent_id) == 3) {
        /* Bone name might well contain quotation marks. Cant be bothered to remove atm. */
        skeleton_add_bone(s, name, id, parent_id);
      }
      
      if (strstr(line, "end")) {
        state = state_load_empty;
      }
    }
    
    else if (state == state_load_skeleton) {
      int id;
      float x, y, z, rx, ry, rz;
      if (sscanf(line, "%i %f %f %f %f %f %f", &id, &x, &y, &z, &rx, &ry, &rz) == 7) {
        bone* b = skeleton_bone_id(s, id);
        /* Swap z and y */
        b->position = v3(x, z, y);
        
        matrix_4x4 rotation = m44_rotation_euler(rx, ry, rz);
        matrix_4x4 handedflip = m44(1,0,0,0,
                                    0,0,1,0,
                                    0,1,0,0,
                                    0,0,0,1);
        
        rotation = m44_mul_m44(handedflip, rotation);
        rotation = m44_mul_m44(rotation, handedflip);
        rotation = m44_transpose(rotation);
        b->rotation = rotation;
        
      }
      
      if (strstr(line, "end")) {
        state = state_load_empty;
      }
    }
  }
  
  SDL_RWclose(file);
  
  skeleton_gen_inv_transforms(s);
  
  return s;
}
