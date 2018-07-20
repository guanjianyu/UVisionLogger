// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Camera/CameraActor.h"
#include "Engine.h"
#include "UnrealEd.h"
#include "bson.h"
#include "mongoc.h"
#include "RawDataAsyncWorker.h"
#include "Runtime/Core/Public/Async/AsyncWork.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UVCamera.generated.h"

/**
 * 
 */
UCLASS()
class UVISIONLOGGER_API AUVCamera : public ACameraActor
{
	GENERATED_BODY()
public:
	AUVCamera();
	~AUVCamera();
	//Set UVCamera id
	UPROPERTY(EditAnyWhere, Category = "Vision Settings")
		FString CameraId;
	//Set UVCamera follow first player view
	UPROPERTY(EditAnyWhere, Category = "Vision Settings")
		bool bFirstPersonView;

	// Set Caputure Image Size the same as Viewport Size
	UPROPERTY(EditAnywhere, Category = "Vision Settings")
		bool bImageSameSize;

	// Camera Width
	UPROPERTY(EditAnywhere, Category = "Vision Settings")
		uint32 Width;

	// Camera Height
	UPROPERTY(EditAnywhere, Category = "Vision Settings")
		uint32 Height;

	// Camera field of view
	UPROPERTY(EditAnywhere, Category = "Vision Settings")
		float FieldOfView;

	// Camera update rate
	UPROPERTY(EditAnywhere, Category = "Vision Settings")
		float FrameRate;

	// Capture Color image
	UPROPERTY(EditAnywhere, Category = "Vision Settings|Capture Mode")
		bool bCaptureColorImage;

	// Capture Depth image
	UPROPERTY(EditAnywhere, Category = "Vision Settings|Capture Mode")
		bool bCaptureDepthImage;

	// Capture Mask image
	UPROPERTY(EditAnywhere, Category = "Vision Settings|Capture Mode")
		bool bCaptureMaskImage;

	// CaptureViewport
	UPROPERTY(EditAnywhere, Category = "Vision Settings|Capture Mode|Color Mode", meta = (EditCondition = bCaptureColorImage))
		bool bCaptureViewport;
	UPROPERTY(EditAnywhere, Category = "Vision Settings|Capture Mode|Color Mode", meta = (EditCondition = bCaptureColorImage))
		bool bCaptureScencComponent;

	// Save data as image
	UPROPERTY(EditAnywhere, Category = "Vision Settings|Capture Mode|Save Mode")
		bool bSaveAsImage;

	// Save All in one bson file
	UPROPERTY(EditAnyWhere, Category = "Vision Settings|Capture Mode|Save Mode")
		bool bSaveAsBsonFile;

	// Save All in MongoDB
	UPROPERTY(EditAnyWhere, Category = "Vision Settings|Capture Mode|Save Mode")
		bool bSaveInMongoDB;

	// Mongo DB IP 
	UPROPERTY(EditAnywhere, Category = "Vision Settings|Capture Mode|Save Mode|MongoDB", meta = (EditCondition = bSaveInMongoDB))
		FString MongoIp;

	// Mongo DB port
	UPROPERTY(EditAnywhere, Category = "Vision Settings|Capture Mode|Save Mode|MongoDB", meta = (EditCondition = bSaveInMongoDB))
		int MongoPort;

	// Mongo DB Name
	UPROPERTY(EditAnywhere, Category = "Vision Settings|Capture Mode|Save Mode|MongoDB", meta = (EditCondition = bSaveInMongoDB))
		FString MongoDBName;

	// Mongo DB Collection name
	UPROPERTY(EditAnywhere, Category = "Vision Settings|Capture Mode|Save Mode|MongoDB", meta = (EditCondition = bSaveInMongoDB))
		FString MongoCollectionName;




protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;

public:
	// Called every frame
	virtual void Tick(float DeltaTime) override;

	// Called when the game ends
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	// initialize Componets
	void Initial();

	// Change the framerate on the fly
	void SetFramerate(const float NewFramerate);

	// Initate AsyncTask
	void InitAsyncTask(FAsyncTask<RawDataAsyncWorker>* AsyncWorker, TArray<FColor>& image, FDateTime Stamp, FString Name, int Width, int Height);

	// Start AsyncTask
	void CurrentAsyncTask(FAsyncTask<RawDataAsyncWorker>* AsyncWorker);

	// Stop AsyncTask
	void StopAsyncTask(FAsyncTask<RawDataAsyncWorker>* AsyncWorker);

private:
	// Camera capture component for color images (RGB)
	FViewport* ColorViewport;
	USceneCaptureComponent2D* ColorImgCaptureComp;

	// Camera capture component for depth images (Depth)
	USceneCaptureComponent2D* DepthImgCaptureComp;

	// Camera capture component for object mask
	USceneCaptureComponent2D* MaskImgCaptureComp;

	// Material instance to get the depth data
	UMaterial* MaterialDepthInstance;

	// Color image buffer
	TArray<FColor> ColorImage;
	TArray<uint8> ColorImageUint;

	// Depth image buffer
	TArray<FColor> DepthImage;
	TArray<uint8> DepthImageUint;

	// Mask image buffer
	TArray<FColor> MaskImage;
	TArray<uint8> MaskImageUint;

	// Image Wrapper for saving image
	TSharedPtr<IImageWrapper> ImageWrapper;

	FRenderCommandFence PixelFence;


	// Pointer to monge database
	mongoc_client_t *client;
	mongoc_database_t *database;
	mongoc_collection_t *collection;

	// bson file writer
	bson_writer_t *writer;
	uint8_t *buf;
	size_t buflen;

	// Intial Asynctask
	bool bInitialAsyncTask;
	
	// helper bool for Reading and Saving 
	bool bFirsttick;
	bool bColorSave;
	bool bDepthSave;
	bool bMaskSave;
	bool bConnectionMongo;

	// Array of objects' colors
	TArray<FColor> ObjectColors;

	// Array of Actors in world
	TArray<FString> ObjectCategory;

	// Map a specfic color for each paintable object
	TMap<FString, uint32> ObjectToColor;

	// number of Used Colors
	uint32 ColorsUsed;

	// Timer callback (timer tick)
	void TimerTick();

	// Read Color raw data
	void ProcessColorImg();

	// Read Depth raw data
	void ProcessDepthImg();

	// Read Mask raw data
	void ProcessMaskImg();

	// Read Raw data from FViewport
	void ReadPixels(FViewport *& viewport, TArray<FColor>& OutImageData, FReadSurfaceDataFlags InFlags = FReadSurfaceDataFlags(RCM_UNorm, CubeFace_MAX), FIntRect InRect = FIntRect(0, 0, 0, 0));

	// Read Raw data from USceneCaptureComponent2D
	void ReadPixels(FTextureRenderTargetResource*& RenderResource, TArray< FColor >& OutImageData, FReadSurfaceDataFlags InFlags = FReadSurfaceDataFlags(RCM_UNorm, CubeFace_MAX), FIntRect InRect = FIntRect(0, 0, 0, 0));

	// Connect MongoDB
	bool ConnectMongo(FString& MongoIp, int& MongoPort, FString& MongoDBName, FString& MongoCollection);

	// Save image in MongoDB
	void SaveImageInMongo(FDateTime Stamp);
	
	// Save Image in Bson file
	void SaveImageAsBson(FDateTime Stamp);

	// Color All Actor in World
	bool ColorAllObjects();

	// Color each Actor with specific color
	bool ColorObject(AActor *Actor, const FString &name);

	// Generate Colors for each Actor
	void GenerateColors(const uint32_t NumberOfColors);

	void SetFileHandle();

	static void BasicSetting(FEngineShowFlags& ShowFlags);

	/** ViewMode for object instance mask */
	static void VertexColor(FEngineShowFlags& ShowFlags);

	/** ViewMode for ground truth types implemented with PostProcess material */
	static void PostProcess(FEngineShowFlags& ShowFlags);

	/** Use the source flags to set the object visibility of target */
	static void SetVisibility(FEngineShowFlags& Target, FEngineShowFlags& Source);

	//Async worker to save the color raw data
	FAsyncTask<RawDataAsyncWorker>* ColorAsyncWorker;

	//Async worker to save the Depth raw data
	FAsyncTask<RawDataAsyncWorker>* DepthAsyncWorker;

	//Async worker to save the Depth raw data
	FAsyncTask<RawDataAsyncWorker>* MaskAsyncWorker;

	// File handle to write the raw data to file
	IFileHandle* FileHandle;

	
};
