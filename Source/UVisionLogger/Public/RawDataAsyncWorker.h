// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Runtime/Core/Public/Async/AsyncWork.h"
#include "Runtime/ImageWrapper/Public/IImageWrapper.h"
#include "Runtime/ImageWrapper/Public/IImageWrapperModule.h"

/**
 * 
 */
class UVISIONLOGGER_API RawDataAsyncWorker : public FNonAbandonableTask
{
private:
	int Width;
	int Height;
	FDateTime TimeStamp;
	FString ImageName;
	TSharedPtr<IImageWrapper> ImageWrapper;
	TArray<FColor> Image;
public:
	RawDataAsyncWorker(TArray<FColor>& Image_init, TSharedPtr<IImageWrapper>& ImageWrapperRef, FDateTime Stamp, FString Name, int Width_init, int Height_init);
	~RawDataAsyncWorker();
	FORCEINLINE TStatId GetStatId() const;
	void DoWork();
	void SetLogToImage();
	void SaveImage(TArray<FColor>&image, TSharedPtr<IImageWrapper> &ImageWrapper, FDateTime Stamp, FString ImageName, int Width, int Height);
};
