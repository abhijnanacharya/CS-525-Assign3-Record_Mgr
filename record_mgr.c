#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "record_mgr.h"
#include "buffer_mgr.h"
#include "storage_mgr.h"
#include "constants.h"

bool recordFeatureFlag = false;
// Structure for Record Controller for containing all the related information
typedef struct __attribute__((packed)) RM_RecordController
{
    BM_BufferPool buffPoolMgmt; // Buffer Pool Management Information & its attributes
    BM_PageHandle bmPageHandle; // This is to access Page Files
    Expr *condition;            // This is for scanning the records in the table
    RID recID;                  // For each record ID
    int freePagesNum;           // Store the freePagesNum details
    int totalScanCount;         // To store the count of the scanned records
    int totalRecordCount;       // This is to store the total number of records in the table
} RM_RecordController;

RM_RecordController *recController;
// This function returns a free slot within a page
int findFreeSlot(char *data, int recordSize)
{
    int i = 0;
    while (i < (PAGE_SIZE / recordSize))
    {
        if (data[recordSize * i] != '+')
            return i;
        i++;
    }
    return -1;
}

int checkRecordInitFlag()
{
    if (recordFeatureFlag == true)
        return RC_OK;
    return -1;
}

// Check if the record manager is initialized, if not it will initialize it.
RC initRecordManager(void *mgmtData)
{
    if (checkRecordInitFlag() != RC_OK)
    {
        recordFeatureFlag = true;
        printf("**********************************************************************************\n");
        printf("******************   RECORD MANAGER SUCCESSFULLY INITIALIZED   *******************\n");
        printf("**********************************************************************************\n");
        initStorageManager();
    }
    else
        printf("\n *******RECORD MANAGER ALREADY INITIALIZED*******\n");
    return RC_OK;
}

// Shutdown the record manager and set the initialization flag to 0 (reset)
RC shutdownRecordManager()
{
    printf("**********************************************************************************\n");
    printf("*********************        RECORD MANAGER SHUTDOWN         **********************\n");
    printf("**********************************************************************************\n");
    recordFeatureFlag = false;
    recController = 0;
    // Free the occupied memory
    free(recController);
    return RC_OK;
}

// Retrieve schema pointer which stores all kinds of stats values
Schema *getSchema(SM_PageHandle pageHandle)
{
    int szint = sizeof(int);
    int num_attrs = *(int *)pageHandle; // Get the number of attributes from the page file
    pageHandle += szint;

    // Allocate memory for the Schema struct
    Schema *schema_ptr = (Schema *)malloc(sizeof(Schema));

    // Initialize the Schema struct members
    schema_ptr->numAttr = num_attrs;
    schema_ptr->attrNames = malloc(sizeof(char *) * num_attrs);
    schema_ptr->dataTypes = malloc(sizeof(DataType) * num_attrs);
    schema_ptr->typeLength = malloc(sizeof(int) * num_attrs);

    // Allocate memory space for storing attribute name for each attribute
    int i = 0;
    while (i < num_attrs)
    {
        schema_ptr->attrNames[i] = (char *)malloc(ATTRIBUTE_SIZE);
        i++;
    }

    int index = 0;
    for (; index < num_attrs;)
    {
        strncpy(schema_ptr->attrNames[index], pageHandle, ATTRIBUTE_SIZE); // Assign attribute names from pageHandle
        pageHandle += ATTRIBUTE_SIZE;
        schema_ptr->dataTypes[index] = *pageHandle; // Assign attribute data type from pageHandle
        pageHandle += szint;
        schema_ptr->typeLength[index] = *pageHandle; // Assign attribute data type length from pageHandle
        pageHandle += szint;
        index++;
    }
    return schema_ptr;
}

// Creates a table of the given name and schema provided.
extern RC createTable(char *name, Schema *schema)
{ // Allocate memory for the record controller
    recController = (RM_RecordController *)calloc(1, sizeof(RM_RecordController));
    // Initialize the buffer pool
    initBufferPool(&recController->buffPoolMgmt, name, MAX_PAGES, RS_FIFO, NULL);

    // Prepare the data to be written to the first page
    char pageData[PAGE_SIZE];
    char *dataPtr = pageData;

    // Set the number of records in the page to 0
    int zeroValue = 0;
    memcpy(dataPtr, &zeroValue, sizeof(int));
    dataPtr += sizeof(int);

    // Set the page number to 1 (first page)
    int pageNum = 1;
    memcpy(dataPtr, &pageNum, sizeof(int));
    dataPtr += sizeof(int);

    // Store the number of attributes in the schema
    memcpy(dataPtr, &schema->numAttr, sizeof(int));
    dataPtr += sizeof(int);

    // Store the key size
    memcpy(dataPtr, &schema->keySize, sizeof(int));
    dataPtr += sizeof(int);

    // Store attribute details
    int index = 0;
    for (; index < schema->numAttr;)
    { // Copy the attribute names
        strncpy(dataPtr, schema->attrNames[index], ATTRIBUTE_SIZE);
        dataPtr += ATTRIBUTE_SIZE;

        // Store the data type
        memcpy(dataPtr, &schema->dataTypes[index], sizeof(DataType));
        dataPtr += sizeof(DataType);
        // Store the type length
        memcpy(dataPtr, &schema->typeLength[index], sizeof(int));
        dataPtr += sizeof(int);
        index = index + 1;
    }

    SM_FileHandle fileHandler;

    // Create and open a PageFile with the given name.
    if (createPageFile(name) != RC_OK || openPageFile(name, &fileHandler) != RC_OK)
        return RC_FILE_NOT_FOUND;

    int writeCode = writeBlock(0, &fileHandler, pageData);
    if (writeCode == RC_OK)
    {
        // Close the file and free the record controller
        return closePageFile(&fileHandler);
        recController = NULL; // dump the ptr
        free(recController);
    }
    return writeCode;
}

// Open a table of the given name and relation provided for any operation to be performed.
extern RC openTable(RM_TableData *rel, char *name)
{
    int sz = sizeof(int); // Compiler dependent x86 assuming
    // Pin the first page (page 0) of the table
    pinPage(&recController->buffPoolMgmt, &recController->bmPageHandle, 0);

    // Assign the initial pointer (starting location) to the page data
    SM_PageHandle pageHandle = (SM_PageHandle *)recController->bmPageHandle.data;

    // Get the total number of tuples from the page file
    //recController->totalRecordCount = (int *)*pageHandle;
    recController->totalRecordCount = *((int *)pageHandle);
    
    pageHandle += sz;

    // Get the free page number from the page file
    recController->freePagesNum = *pageHandle;
    pageHandle += sz;

    // Set the management data pointer of the table data structure
    rel->mgmtData = (RM_RecordController *)recController;

    // Set the table name in the table data structure
    rel->name = name;

    // Get the schema for the table by parsing the page data
    rel->schema = getSchema(pageHandle);

    // Unpin the first page of the table
    printf("\n UNPINNING PAGE \n");
    unpinPage(&recController->buffPoolMgmt, &recController->bmPageHandle);

    // Force the changes to be written to disk
    printf("\n WRITING CHANGES TO DISK \n");
    forcePage(&recController->buffPoolMgmt, &recController->bmPageHandle);

    return RC_OK;
}

// Close a table associated with the given relation after all the operations to be performed are done.
extern RC closeTable(RM_TableData *rel)
{
    if (shutdownBufferPool(&((RM_RecordController *)rel->mgmtData)->buffPoolMgmt))
        return RC_OK;
    else
        return RC_ERROR; // FAILURE TO CLOSE BUFFER
}

// Deletes a table with the given name after all the operations to be performed are done.
extern RC deleteTable(char *name)
{
    return destroyPageFile((char *)name);
}

// Retrieve the Total Number of Tuples in the Table
extern int getNumTuples(RM_TableData *rel)
{
    return ((RM_RecordController *)rel->mgmtData)->totalRecordCount;
}

//************************************************************************************************************************************************************
// insertRecord() inserts a new Record (record) into the table (rel).
extern RC insertRecord(RM_TableData *rel, Record *record)
{
    RC RC_code = RC_OK;
    int recordSize;
    RM_RecordController *recController = rel->mgmtData;
    int sz = getRecordSize(rel->schema);
    if (sz >= 0)
        recordSize = sz;

    RID *rid = &record->id;
    rid->page = recController->freePagesNum;

    if (pinPage(&recController->buffPoolMgmt, &recController->bmPageHandle, rid->page) == RC_OK)
    {
        char *dataInsert = recController->bmPageHandle.data;

        int fSlot = findFreeSlot(dataInsert, recordSize);
        if (fSlot >= 0)
            rid->slot = fSlot;
        while (rid->slot == -1)
        {

            if (unpinPage(&recController->buffPoolMgmt, &recController->bmPageHandle) == RC_OK)
            {
                rid->page += 1;
                if (pinPage(&recController->buffPoolMgmt, &recController->bmPageHandle, rid->page) == RC_OK)
                {
                    dataInsert = recController->bmPageHandle.data;
                    rid->slot = findFreeSlot(dataInsert, recordSize);
                }
                else
                {
                    printf("FATAL: FAILED TO ENTER");
                    return RC_ERROR;
                }
            }
        }
        if (markDirty(&recController->buffPoolMgmt, &recController->bmPageHandle) == RC_OK)
        {
            char *freeSlotPtr = dataInsert + (rid->slot * recordSize);
            *freeSlotPtr = '+'; // Mark as free
            memcpy(++freeSlotPtr, record->data + 1, recordSize - 1);

            if (unpinPage(&recController->buffPoolMgmt, &recController->bmPageHandle) == RC_OK)
            {
                recController->totalRecordCount++;
                if (pinPage(&recController->buffPoolMgmt, &recController->bmPageHandle, 0) == RC_OK)
                    return RC_OK;
            }
            else
            {
                return RC_ERROR;
            }

            return RC_code;
        }
    }
}




// The "getRecordSize" function is to retrieve the record size
// Helper function to calculate the size of each data type
int dataTypeSize(DataType type) {
    switch(type) {
        case DT_INT:
            return sizeof(int);
        case DT_FLOAT:
            return sizeof(float);
        case DT_BOOL:
            return sizeof(bool);
        default:
            return 0; // Handle unknown data types or error conditions
    }
}

extern int getRecordSize(Schema *schema) {
    int recordSizeCount = 0;

    for (int i = 0; i < schema->numAttr; i++) {
        DataType type = schema->dataTypes[i];
        if (type == DT_STRING) {
            recordSizeCount += schema->typeLength[i];
        } else {
            recordSizeCount += dataTypeSize(type);
        }
    }
    return recordSizeCount;
}


// This function removes a schema from memory and de-allocates all the memory space allocated to the schema.

extern RC freeSchema(Schema *schema)
{
    if (schema == NULL) // Check if the schema pointer is NULL
        return RC_OK;

    // Freeing space occupied by 'schema' attribute names and typeLength array
    for (int i = 0; i < schema->numAttr; i++)
    {
        if (schema->attrNames[i] != NULL) // Check if the attribute name pointer is not NULL
        {
            free(schema->attrNames[i]);
            schema->attrNames[i] = NULL; // Set the attribute name pointer to NULL after freeing
        }
    }

    // Freeing space occupied by 'schema' attribute names array and typeLength array
    free(schema->attrNames);
    free(schema->typeLength);

    // Freeing space occupied by 'schema' itself
    free(schema);

    return RC_OK;
}






// The "deleteRecord" function is to delete the existing record from the table.
extern RC deleteRecord(RM_TableData *rel, RID id)
{
    int recordSize;
    // Getting our details from mgmtData
    RM_RecordController *recController = (RM_RecordController *)rel->mgmtData;

    // Pinning the page to update
    if (pinPage(&recController->buffPoolMgmt, &recController->bmPageHandle, id.page) == RC_OK)
    {
        // get size of corresponding record
        int rsize = getRecordSize(rel->schema);
        if (rsize != NULL)
        {
            recordSize = rsize;
        }

        recController->freePagesNum = id.page;
        //char *size= (id.slot * recordSize);
        char *size = (char *)(id.slot * recordSize);
        char *data = recController->bmPageHandle.data;
        data = data + (int)size;
        // Deleted Record Demarkation using - [TOMBSTONE] ☠️
        *data = '-';

        // Marking page dirty
        if (markDirty(&recController->buffPoolMgmt, &recController->bmPageHandle) == RC_OK)
        {
            // Unpinning the page
            unpinPage(&recController->buffPoolMgmt, &recController->bmPageHandle);
        }
        else
        {
            return RC_FATAL_ERROR;
        }
    }

    return RC_OK;
}

// The "getRecord" function is to retrieve the existing record from the table.
extern RC getRecord(RM_TableData *rel, RID id, Record *record)
{
    // Getting our details from mgmtData
    RM_RecordController *recController = rel->mgmtData;

    // Pinning the page to update
    if (pinPage(&recController->buffPoolMgmt, &recController->bmPageHandle, id.page) == RC_OK)
    {
        char *dataPtr = recController->bmPageHandle.data;

        // get size of corresponding record
        int recordSize = getRecordSize(rel->schema);
        int offset = id.slot * recordSize;
        dataPtr = dataPtr + offset;

        // Check if no record matches
        if (*dataPtr != '+')
        {
            return RC_RM_NO_TUPLE_WITH_GIVEN_RID;
        }
        else
        {
            record->id = id;
            char *data = record->data;

            // Copying data
            data += 1;
            memcpy(data, dataPtr + 1, recordSize - 1);
        }

        // Unpinning the page
        if (unpinPage(&recController->buffPoolMgmt, &recController->bmPageHandle) == RC_OK)
        {
            return RC_OK;
        }
    }

    return RC_OK;
}



// The "startScan" function is to initialize all attributes of RM_ScanHandle
extern RC startScan(RM_TableData *rel, RM_ScanHandle *scan, Expr *cond)
{
    // Pre-requisite check
    if (cond == NULL)
    {
        return RC_SCAN_CONDITION_NOT_FOUND;
    }

    // Opening the table for scanning
    openTable(rel, "ScanTable");

    // Allocate memory for scan controller
    RM_RecordController *scanController = (RM_RecordController *)calloc(1, sizeof(RM_RecordController));
    if (scanController == NULL)
    {
        return RC_MEM_ALLOC_FAILED;
    }

    // Initialize scan controller attributes
    scanController->condition = cond;
    scanController->recID.page = 1; // Start from page 1
    scanController->recID.slot = 0; // Start from slot 0

    // Set scan handle attributes
    scan->mgmtData = scanController;
    scan->rel = rel;

    return RC_OK;
}


extern RC next(RM_ScanHandle *scan, Record *record)
{
    RM_RecordController *scanController = (RM_RecordController *)scan->mgmtData;
    RM_RecordController *tableController = (RM_RecordController *)scan->rel->mgmtData;
    Schema *schema = scan->rel->schema;
    int recSize = getRecordSize(schema);
    int totalSlots = PAGE_SIZE / recSize;

    if (scanController->condition == NULL)
        return RC_SCAN_CONDITION_NOT_FOUND;

    if (tableController->totalRecordCount == 0)
        return RC_RM_NO_MORE_TUPLES;

    int slot = scanController->totalScanCount % totalSlots;
    int page = 1 + (scanController->totalScanCount / totalSlots);

    pinPage(&tableController->buffPoolMgmt, &scanController->bmPageHandle, page);

    char *pageHandleData = scanController->bmPageHandle.data;
    char *slotData = pageHandleData + (slot * recSize);

    record->id.page = page;
    record->id.slot = slot;

    record->data[0] = '-'; // [TOMBSTONE] ☠️ Mark as deleted
    memcpy(record->data + 1, slotData + 1, recSize - 1);

    scanController->totalScanCount++;

    Value *res = (Value *)malloc(sizeof(Value));
    evalExpr(record, schema, scanController->condition, &res);

    unpinPage(&tableController->buffPoolMgmt, &scanController->bmPageHandle);

    if (res->v.boolV == TRUE)
    {
        return RC_OK;
    }

    if (scanController->totalScanCount < tableController->totalRecordCount)
    {
        return next(scan, record);
    }
    else
    {
        scanController->totalScanCount = 0;
        return RC_RM_NO_MORE_TUPLES;
    }
}


extern RC closeScan(RM_ScanHandle *scan)
{
    if (scan == NULL) return RC_OK;

    RM_RecordController *scanController = (RM_RecordController *)scan->mgmtData;

    if (scanController != NULL)
    {
        if (scanController->totalScanCount > 0)
        {
            RM_RecordController *recController = (RM_RecordController *)scan->rel->mgmtData;
            unpinPage(&recController->buffPoolMgmt, &scanController->bmPageHandle);

            scanController->totalScanCount = 0;
            scanController->recID.slot = 0;
            scanController->recID.page = 1;
        }

        free(scan->mgmtData);
        scan->mgmtData = NULL;
    }

    return RC_OK;
}


// The "createSchema" function is to make a new schema and initialize its all attributes
// Define a helper function to allocate memory and initialize schema attributes
extern Schema *createSchema(int numAttr, char **attrNames, DataType *dataTypes, int *typeLength, int keySize, int *keys)
{
    // Allocate memory for the schema structure
    Schema *currentSchema = (Schema *)malloc(sizeof(Schema));
    if (currentSchema == NULL) {
        // Handle memory allocation failure
        return NULL;
    }

    // Initialize the schema attributes
    currentSchema->numAttr = numAttr;

    // Allocate memory for attribute names and copy each name
    currentSchema->attrNames = (char **)malloc(numAttr * sizeof(char *));
    if (currentSchema->attrNames == NULL) {
        // Handle memory allocation failure
        free(currentSchema);
        return NULL;
    }
    for (int i = 0; i < numAttr; i++) {
        currentSchema->attrNames[i] = strdup(attrNames[i]);
        if (currentSchema->attrNames[i] == NULL) {
            // Handle memory allocation failure
            for (int j = 0; j < i; j++) {
                free(currentSchema->attrNames[j]);
            }
            free(currentSchema->attrNames);
            free(currentSchema);
            return NULL;
        }
    }

    // Allocate memory for data types and copy each type
    currentSchema->dataTypes = (DataType *)malloc(numAttr * sizeof(DataType));
    if (currentSchema->dataTypes == NULL) {
        // Handle memory allocation failure
        free(currentSchema->attrNames);
        free(currentSchema);
        return NULL;
    }
    memcpy(currentSchema->dataTypes, dataTypes, numAttr * sizeof(DataType));

    // Allocate memory for type lengths and copy each length
    currentSchema->typeLength = (int *)malloc(numAttr * sizeof(int));
    if (currentSchema->typeLength == NULL) {
        // Handle memory allocation failure
        free(currentSchema->dataTypes);
        free(currentSchema->attrNames);
        free(currentSchema);
        return NULL;
    }
    memcpy(currentSchema->typeLength, typeLength, numAttr * sizeof(int));

    // Set the key size and copy key attributes
    currentSchema->keySize = keySize;
    if (keySize > 0) {
        currentSchema->keyAttrs = (int *)malloc(keySize * sizeof(int));
        if (currentSchema->keyAttrs == NULL) {
            // Handle memory allocation failure
            free(currentSchema->typeLength);
            free(currentSchema->dataTypes);
            free(currentSchema->attrNames);
            free(currentSchema);
            return NULL;
        }
        memcpy(currentSchema->keyAttrs, keys, keySize * sizeof(int));
    } else {
        currentSchema->keyAttrs = NULL;
    }

    return currentSchema;
}







// The function is used to update a Record
extern RC updateRecord(RM_TableData *rel, Record *rec)
{
    RM_RecordController *recController = rel->mgmtData;
    if (pinPage(&recController->buffPoolMgmt, &recController->bmPageHandle, rec->id.page) == RC_OK)
    {
        int recordSize = getRecordSize(rel->schema);
        RID id = rec->id;
        int offset = id.slot * recordSize;
        char *data = recController->bmPageHandle.data;
        data = data + offset;

        *data = '+';

        memcpy(++data, rec->data + 1, recordSize - 1);

        if (markDirty(&recController->buffPoolMgmt, &recController->bmPageHandle) == RC_OK)
        {
            if (unpinPage(&recController->buffPoolMgmt, &recController->bmPageHandle) == RC_OK)
            {
                return RC_OK;
            }
        }
    }
    return RC_OK;
}

// This function creates a new record in the schema
extern RC createRecord(Record **record, Schema *schema)
{
    RC returnCode = RC_OK;
    int recordSize;
    int rSize = getRecordSize(schema);
    if (rSize >= 0)
    {
        recordSize = rSize;
        Record *newrec = malloc(sizeof(Record));
        newrec->data = (char *)malloc(recordSize);
        newrec->id.page = -1;
        newrec->id.slot = -1;

        char *dataptr = newrec->data;

        *dataptr = '-';
        dataptr++;
        *(dataptr) = '\0';
        *record = newrec;
    }
    return returnCode;
}

// ing the record created earlier
extern RC freeRecord(Record *record)
{
    if (record != NULL)
    {
        record = NULL;
        free(record);
    }
    return RC_OK;
}

// This method is used to calculate the Offset associated
RC attrOffset(Schema *schema, int attrNum, int *res)
{
    int szInt = sizeof(int);
    int szBool = sizeof(bool);
    int szFloat = sizeof(float);

    int iter = 0;
    *res = 1;

    while (iter < attrNum)
    {
        DataType ch = schema->dataTypes[iter];
        // Switch depending on DATA TYPE of the ATTRIBUTE
        switch (ch)
        {
        case DT_STRING:
            //  Value is Length of Defined string
            *res = *res + schema->typeLength[iter];
            if (*res >= -1)
            {
                printf("Valid!\n");
            }
            break;
        case DT_BOOL:
            // Adding size of BOOLEAN
            *res = *res + szBool;
            if (*res >= -1)
            {
                printf("Valid!\n");
            }
            break;
        case DT_FLOAT:
            // Adding size of FLOAT
            *res = *res + szFloat;
            if (*res >= -1)
            {
                printf("Valid!\n");
            }
            break;

        case DT_INT:
            // Adding size of INT
            *res = *res + szInt;
            if (*res >= -1)
            {
                printf("Valid!\n");
            }
            break;
        }
        iter++;
    }
    return RC_OK;
}

// This function is used to get the attribute values of a record
extern RC getAttr(Record *rec, Schema *schema, int attrNum, Value **value)
{
    int offset = 0;

    if (attrOffset(schema, attrNum, &offset))
    {
        printf("OFFSET CALCULATED SUCCESSFULLY!\n");
    }

    Value *attr = (Value *)malloc(sizeof(Value));
    char *dataptr = rec->data;
    dataptr = dataptr + offset;
    if (attrNum == 1)
    {
        schema->dataTypes[attrNum] = DT_STRING;
    }
    else
        schema->dataTypes[attrNum];

    if (schema->dataTypes[attrNum] == DT_INT)
    {
        // Retriving attribute value of type Int
        int value = 0;
        memcpy(&value, dataptr, sizeof(int));
        attr->v.intV = value;
        attr->dt = DT_INT;
    }
    else if (schema->dataTypes[attrNum] == DT_STRING)
    {
        // Retriving attribute value of type String
        int length = schema->typeLength[attrNum];
        // creating space for string
        attr->dt = schema->dataTypes[attrNum];
        attr->v.stringV = malloc(length + 1);
        if (attr->v.stringV != NULL) {
            memcpy(attr->v.stringV, dataptr, length);
            attr->v.stringV[length] = '\0'; // Null terminator 
        }

    }
    else if (schema->dataTypes[attrNum] == DT_FLOAT)
    {
        // Retriving attribute value of type FLoat
        float value;
        memcpy(&value, dataptr, sizeof(float));
        attr->v.floatV = value;
        attr->dt = DT_FLOAT;
    }
    else if (schema->dataTypes[attrNum] == DT_FLOAT == DT_BOOL)
    {
        // Retriving attribute value of type Bool
        bool value;
        memcpy(&value, dataptr, sizeof(bool));
        attr->v.boolV = value;
        attr->dt = DT_BOOL;
    }
    else
    {
        printf("Undefined DataType \n");
    }

    *value = attr;
    return RC_OK;
}

// This function is used to get the attribute values of a record
extern RC setAttr(Record *rec, Schema *schema, int attrNum, Value *value)
{
    int offset = 0;

    if (attrOffset(schema, attrNum, &offset) == RC_OK)
    {
        char *dataptr = rec->data;
        dataptr = dataptr + offset;

        if (schema->dataTypes[attrNum] == DT_INT)
        {
            *dataptr = value->v.intV;
            dataptr = dataptr + sizeof(int);
        }
        else if (schema->dataTypes[attrNum] == DT_STRING)
        {
            int length = schema->typeLength[attrNum];
            strncpy(dataptr, value->v.stringV, length);
            length = schema->typeLength[attrNum];
            dataptr = dataptr + length;
        }
        else if (schema->dataTypes[attrNum] == DT_FLOAT)
        {
            // Setting attribute value of an attribute of type FLOAT
            *dataptr = value->v.floatV;
            dataptr = dataptr + sizeof(float);
        }

        else if (schema->dataTypes[attrNum] == DT_BOOL)
        {
            *dataptr = value->v.boolV;
            dataptr = dataptr + sizeof(bool);
        }
        else
        {
            printf("SERIALIZER NOT DEFINED \n");
            return RC_SERIALIZER_ERROR;
        }
    }
    else
    {
        return RC_SERIALIZER_ERROR;
    }

    return RC_OK;
}
