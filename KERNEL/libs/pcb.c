
#include "pcb.h"

/*
 * Serializa un pcb a una cadena de bytes.
 *
 * pcb : El puntero al pcb que se quiere serializar.
 * buffer : El buffer donde se almacenaran los bytes del pcb.
 * buffer_size : El tamanio del buffer, que terminara siendo el total de bytes que componen al pcb.
 */
void serialize_pcb(t_pcb *pcb, void **buffer, size_t *buffer_size) {

    serialize_data(&pcb->pid, sizeof(pid_t), buffer, buffer_size);
    serialize_data(&pcb->program_counter, sizeof(uint32_t), buffer, buffer_size);
    serialize_data(&pcb->stack_pointer, sizeof(uint32_t), buffer, buffer_size);
    serialize_stack(pcb->stack_index, buffer, buffer_size);
    serialize_data(&pcb->status, sizeof(int), buffer, buffer_size);
    serialize_data(&pcb->instrucciones_size, sizeof(t_size), buffer, buffer_size);
    serialize_instrucciones(pcb->instrucciones_serializado, pcb->instrucciones_size, buffer, buffer_size);
    serialize_data(&pcb->etiquetas_size, sizeof(t_size), buffer, buffer_size);
    serialize_data(pcb->etiquetas, pcb->etiquetas_size, buffer, buffer_size);
}

void serialize_instrucciones(t_intructions *instrucciones, size_t instrucciones_size, void **buffer, size_t *buffer_size) {
    int indice = 0;
    for(indice = 0; indice < instrucciones_size ; indice++) {
        serialize_t_instructions(instrucciones+indice, buffer, buffer_size);
    }
}

/*
 * Deserializa un pcb a una cadena de bytes.
 *
 * pcb : El puntero al pcb que se habra deserializado.
 * serialized_data : El buffer donde estaran almacenados los bytes del pcb.
 * serialized_data_index : El indice que indica desde que byte se quiere deserializar.
 */

//void deserialize_data(void *object, size_t nBytes, void **serialized_data, size_t *lastIndex)
void deserialize_pcb(t_pcb **pcb, void *serialized_data, size_t *serialized_data_index) {
    deserialize_data(&(*pcb)->pid, sizeof(pid_t), serialized_data, serialized_data_index);
    deserialize_data(&(*pcb)->program_counter, sizeof(uint32_t), serialized_data, serialized_data_index);
    deserialize_data(&(*pcb)->stack_pointer, sizeof(uint32_t), serialized_data, serialized_data_index);
    (*pcb)->stack_index = queue_create(); //TODO: Por que se necesita esto aca y en deserialize_stack TAMBIEN?
    deserialize_stack(&(*pcb)->stack_index, serialized_data, serialized_data_index);
    deserialize_data(&(*pcb)->status, sizeof(int), serialized_data, serialized_data_index);
    deserialize_data(&(*pcb)->instrucciones_size, sizeof(t_size), serialized_data, serialized_data_index);
    deserialize_instrucciones(&(*pcb)->instrucciones_serializado, (*pcb)->instrucciones_size, serialized_data, serialized_data_index);
    deserialize_data(&(*pcb)->etiquetas_size, sizeof(t_size), serialized_data, serialized_data_index);
    deserialize_data(&(*pcb)->etiquetas, (*pcb)->etiquetas_size, serialized_data, serialized_data_index);
}


void serialize_t_instructions(t_intructions *intructions, void **buffer, size_t *buffer_size) {
    serialize_data(&intructions->start, sizeof(t_puntero_instruccion), buffer, buffer_size);
    serialize_data(&intructions->offset, sizeof(t_size), buffer, buffer_size);
}

void deserialize_instrucciones(t_intructions **instrucciones, t_size instrucciones_size, void **serialized_data, size_t *serialized_data_size) {
    *instrucciones = calloc(instrucciones_size, sizeof(t_intructions));
    int indice = 0;
    //instrucciones_size tiene la cantidad de instrucciones cargadas
    for(indice = 0; indice < instrucciones_size; indice ++) {
        deserialize_data(&(*instrucciones+indice)->start, sizeof(t_puntero_instruccion), serialized_data, serialized_data_size);
        deserialize_data(&(*instrucciones+indice)->offset, sizeof(t_size), serialized_data, serialized_data_size);
    }
}