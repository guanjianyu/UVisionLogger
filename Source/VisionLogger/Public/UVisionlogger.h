// Copyright 2018, Institute for Artificial Intelligence - University of Bremen

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "RawDataAsyncWorker.h"
#include "Runtime/Core/Public/Async/AsyncWork.h"
#include "Engine/TextureRenderTarget2D.h"
#include "StaticMeshResources.h"
#include "Runtime/Engine/Public/Slate/SceneViewport.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UVisionlogger.generated.h"


UCLASS()
class VISIONLOGGER_API AUVisionlogger : public AActor
{
	GENERATED_BODY()
	
public:	
	// Sets default values for this actor's properties
	AUVisionlogger();

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

	// Mongo DB IP 
	UPROPERTY(EditAnywhere, Category = "Vision Settings|MongoDB")
		FString MongoIp;

	// Mongo DB port
	UPROPERTY(EditAnywhere, Category = "Vision Settings|MongoDB")
		uint16 MongoPort;

	// Mongo DB Name
	UPROPERTY(EditAnywhere, Category = "Vision Settings|MongoDB")
		FString MongoDBName;

	// Mongo DB Collection name
	UPROPERTY(EditAnywhere, Category = "Vision Settings|MongoDB")
		FString MongoCollectionName;

	// Capture Color image
	UPROPERTY(EditAnywhere, Category = "Vision Settings|Capture Mode")
		bool bCaptureColorImage;

	// Capture Mask image
	UPROPERTY(EditAnywhere, Category = "Vision Settings|Capture Mode")
		bool bCaptureMaskImage;

	// Capture depth image
	UPROPERTY(EditAnywhere, Category = "Vision Settings|Capture Mode")
		bool bCaptureDepthImage;

	// Save data as image
	UPROPERTY(EditAnywhere, Category = "Vision Settings|Capture Mode|Save Mode")
		bool bSaveAsImage;

	// Save data in MongoDB
	UPROPERTY(EditAnywhere, Category = "Vision Settings|Capture Mode|Save Mode")
		bool bSaveInMongo;

	// Save data in bson file
	UPROPERTY(EditAnywhere, Category = "Vision Settings|Capture Mode|Save Mode")
		bool bSaveAsBson;

	// Intial Asynctask
	bool bInitialAsyncTask;

	

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
	//FViewport* ColorImgCaptureComp;
	FViewport* ColorViewport;
	USceneCaptureComponent2D* ColorImgCaptureComp;
	// Camera capture component for depth image
	USceneCaptureComponent2D* DepthImgCaptureComp;

	// Camera capture component for object mask
	USceneCaptureComponent2D* MaskImgCaptureComp;

	// Material instance to get the depth data
	UMaterial* MaterialDepthInstance;

	// Color image buffer
	TArray<FColor> ColorImage;

	// Mask image buffer
	TArray<FColor> MaskImage;

	// Depth image buffer
	TArray<FColor> DepthImage;

	// Array of objects' colors
	TArray<FColor> ObjectColors;

	// Array of Actors in world
	TArray<FString> ObjectCategory;

	// Map a specfic color for each paintable object
	TMap<FString, uint32> ObjectToColor;

	// number of Used Colors
	uint32 ColorsUsed;

	// Image Wrapper for saving image
	TSharedPtr<IImageWrapper> ImageWrapper;

	FRenderCommandFence ColorPixelFence;
	FRenderCommandFence MaskPixelFence;
	FRenderCommandFence DepthPixelFence;

	// helper bool for Reading and Saving 
	bool bColorFirsttick;
	bool bMaskFirsttick;
	bool bDepthFirsttick;
	bool bColorSave;
	bool bMaskSave;
	bool bDepthSave;

	// booleen Connection to MongoDB
	bool bConnectMongo;

	// Pointer to monge database
	/*mongoc_client_t *client;
	mongoc_database_t *database;
	mongoc_collection_t *collection;*/

	// Color Image Height and Width
	int ColorWidth, ColorHeight;

	// Timer callback (timer tick)
	void TimerTick();

	// Connect MongoDB
	bool ConnectMongo(FString& MongoIp, int& MongoPort, FString& MongoDBName, FString& MongoCollection);
	
	// Read Color raw data
	void ProcessColorImg();

	// Read Mask raw data
	void ProcessMaskImg();

	// Read Mask raw data
	void ProcessDepthImg();

	// Read Raw data from viewport
	void ReadPixels(FSceneViewport* &viewport, TArray< FColor >& OutImageData, FReadSurfaceDataFlags InFlags = FReadSurfaceDataFlags(RCM_UNorm, CubeFace_MAX), FIntRect InRect = FIntRect(0, 0, 0, 0));

	// Read Raw data from USceneCaptureComponent2D
	void ReadPixels(FTextureRenderTargetResource*& RenderResource, TArray< FColor >& OutImageData, FReadSurfaceDataFlags InFlags = FReadSurfaceDataFlags(RCM_UNorm, CubeFace_MAX), FIntRect InRect = FIntRect(0, 0, 0, 0));

	// Color All Actor in World
	bool ColorAllObjects();

	// Color each Actor with specific color
	bool ColorObject(AActor *Actor, const FString &name);

	// Generate Colors for each Actor
	void GenerateColors(const uint32_t NumberOfColors);

	// Change Camera Flags 
	void ShowFlagsBasicSetting(FEngineShowFlags &ShowFlags) const;
	void ShowFlagsLit(FEngineShowFlags &ShowFlags) const;
	void ShowFlagsPostProcess(FEngineShowFlags &ShowFlags) const;
	void ShowFlagsVertexColor(FEngineShowFlags &ShowFlags) const;
    
	//Async worker to save the color raw data
	FAsyncTask<RawDataAsyncWorker>* ColorAsyncWorker;

	//Async worker to save the Mask raw data
	FAsyncTask<RawDataAsyncWorker>* MaskAsyncWorker;

	//Async worker to save the depth raw data
	FAsyncTask<RawDataAsyncWorker>* DepthAsyncWorker;


	
};
